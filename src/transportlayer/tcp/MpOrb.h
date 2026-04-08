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

#ifndef MPORB_TRANSPORTLAYER_TCP_MPORB_H_
#define MPORB_TRANSPORTLAYER_TCP_MPORB_H_

#include "../../../../mptcp/src/transportlayer/tcp/MpTcp.h"

namespace inet {
namespace tcp {

class MpOrb : public MpTcp
{
  protected:
    virtual TcpConnection *createConnection(int socketId) override;
    virtual SubflowConnection *createSubflowConnection(int socketId, MpTcpConnection *metaConn, bool isMaster) override;
};

} // namespace tcp
} // namespace inet

#endif
