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

#ifndef MPORB_TRANSPORTLAYER_TCP_MPORBSUBFLOWCONNECTION_H_
#define MPORB_TRANSPORTLAYER_TCP_MPORBSUBFLOWCONNECTION_H_

#include <vector>

#include "../../../../mptcp/src/transportlayer/tcp/SubflowConnection.h"
#include "../../../../orbtcp/src/common/IntTag_m.h"

namespace inet {
namespace tcp {

class MpOrbSubflowConnection : public SubflowConnection
{
  protected:
    std::vector<IntDataVec> intDataContextStack;

    virtual void pushIntContext(const Ptr<const TcpHeader>& tcpHeader);
    virtual void popIntContext();

  public:
    virtual bool openActive(L3Address localAddr, L3Address remoteAddr, int localPort, int remotePort) override;
    virtual bool openPassive(L3Address localAddr, int localPort) override;
    virtual void setUpConnection(L3Address src, L3Address dest, int srcPort, int destPort) override;

    virtual TcpEventCode processSegment1stThru8th(Packet *tcpSegment, const Ptr<const TcpHeader>& tcpHeader) override;
    virtual bool processAckInEstabEtc(Packet *tcpSegment, const Ptr<const TcpHeader>& tcpHeader) override;
    virtual void sendToIP(Packet *tcpSegment, const Ptr<TcpHeader>& tcpHeader) override;

    virtual void sendIntAck(const IntDataVec& intData);
    virtual IntDataVec getCurrentIntData() const;
};

} // namespace tcp
} // namespace inet

#endif
