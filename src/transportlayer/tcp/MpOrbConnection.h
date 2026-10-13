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

namespace inet {
namespace tcp {

class MpOrbConnection : public MpTcpConnection
{
  public:
    virtual ~MpOrbConnection();
    void startOmegaControl();
    virtual bool processTimer(cMessage *msg) override;
    virtual void prepareForRemoval() override;

  protected:
    // One timer and one rate update for the entire Omega connection.
    cMessage *omegaTimer = nullptr;
    simtime_t omegaInterval;
    simtime_t omegaLastUpdate;
    double omegaRateScale = 0;
    double omegaRateGain = 0;
    double omegaMinRtt = 0;
    void updateOmegaRates();
    virtual std::unique_ptr<MpTcpPacketScheduler> createPacketScheduler(const char *mode) override;

    virtual void process_OPEN_ACTIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg) override;
    virtual void process_OPEN_PASSIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg) override;
};

} // namespace tcp
} // namespace inet

#endif
