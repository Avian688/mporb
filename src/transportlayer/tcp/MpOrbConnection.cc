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

#include "MpOrbConnection.h"

namespace inet {
namespace tcp {

Define_Module(MpOrbConnection);

namespace {
constexpr const char *MPORB_META_ALGORITHM = "MpTcpMetaCubic";
}

void MpOrbConnection::process_OPEN_ACTIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg)
{
    auto *openCmd = check_and_cast<TcpOpenCommand *>(tcpCommand);
    // MpOrb runs the Orb flavour on the subflows; keep the meta connection on the
    // lightweight MPTCP meta algorithm regardless of the user-facing tcpAlgorithmClass.
    openCmd->setTcpAlgorithmClass(MPORB_META_ALGORITHM);

    MpTcpConnection::process_OPEN_ACTIVE(event, tcpCommand, msg);
}

void MpOrbConnection::process_OPEN_PASSIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg)
{
    auto *openCmd = check_and_cast<TcpOpenCommand *>(tcpCommand);
    // Passive meta sockets use the same meta-side algorithm as active ones.
    openCmd->setTcpAlgorithmClass(MPORB_META_ALGORITHM);

    MpTcpConnection::process_OPEN_PASSIVE(event, tcpCommand, msg);
}

} // namespace tcp
} // namespace inet
