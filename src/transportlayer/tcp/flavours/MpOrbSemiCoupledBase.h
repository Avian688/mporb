//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBASE_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBSEMICOUPLEDBASE_H_

#include "MpOrbUncoupled.h"

namespace inet {
namespace tcp {

class MpTcpConnection;

/**
 * Meta-connection access and telemetry identity for semi-coupled MPORB flavours.
 *
 * This class is deliberately not registered as a selectable TCP algorithm.
 */
class MpOrbSemiCoupledBase : public MpOrbUncoupled
{
  protected:
    virtual MpTcpConnection *getMetaConnection() const;

  public:
    virtual size_t getConnId() override;
};

} // namespace tcp
} // namespace inet

#endif
