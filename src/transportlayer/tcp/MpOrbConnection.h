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

#ifndef MPORB_TRANSPORTLAYER_TCP_MPORBCONNECTION_H_
#define MPORB_TRANSPORTLAYER_TCP_MPORBCONNECTION_H_

#include "../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include "../../../../orbtcp/src/common/IntTag_m.h"

#include <map>

namespace inet {
namespace tcp {

class MpOrbConnection : public MpTcpConnection
{
  public:
    struct PressureAllocation {
        double fairRate = 0;
        double subflowRate = 0;
        double connectionRate = 0;
        double weight = 1;
        double weightedFairRate = 0;
        unsigned int freshSubflows = 0;
    };

    void recordPressureFeedback(SubflowConnection *subflow, const IntMetaData& feedback,
            int flowCountBits, int maxFlowCount);
    PressureAllocation getPressureAllocation(const SubflowConnection *subflow) const;
    void forgetPressureFeedback(const SubflowConnection *subflow);
    virtual void removeSubflow(SubflowConnection *subflow) override;

  protected:
    struct PressureFeedback {
        double bandwidth = 0;
        uint32_t flows = 0;
        simtime_t sampledAt = SIMTIME_ZERO;
        simtime_t receivedAt = SIMTIME_ZERO;
    };
    // Module IDs avoid aliasing feedback if a removed subflow's address is reused.
    std::map<int, PressureFeedback> pressureFeedback;

    virtual void process_OPEN_ACTIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg) override;
    virtual void process_OPEN_PASSIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg) override;
};

} // namespace tcp
} // namespace inet

#endif
