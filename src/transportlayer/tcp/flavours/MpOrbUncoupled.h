//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#ifndef MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBUNCOUPLED_H_
#define MPORB_TRANSPORTLAYER_TCP_FLAVOURS_MPORBUNCOUPLED_H_

#include "../../../../../orbtcp/src/transportlayer/orbtcp/flavours/OrbtcpFlavour.h"

namespace inet {
namespace tcp {

class MpOrbUncoupled : public OrbtcpFlavour
{
  protected:
    virtual IntDataVec getCurrentIntData() const;

    virtual void processRexmitTimer(TcpEventCode& event) override;

  public:
    virtual void established(bool active) override;

    virtual void receiveSeqChanged() override;
    virtual void receiveSeqChanged(IntDataVec intData) override;

    virtual void receivedOutOfOrderSegment() override;
    virtual void receivedOutOfOrderSegment(IntDataVec intData) override;

    virtual void receivedDataAck(uint32_t firstSeqAcked) override;
    virtual void receivedDataAck(uint32_t firstSeqAcked, IntDataVec intData) override;

    virtual void receivedDuplicateAck() override;
    virtual void receivedDuplicateAck(uint32_t firstSeqAcked, IntDataVec intData) override;
};

} // namespace tcp
} // namespace inet

#endif
