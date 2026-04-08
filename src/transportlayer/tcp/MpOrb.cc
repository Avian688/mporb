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

#include "MpOrb.h"

namespace inet {
namespace tcp {

Define_Module(MpOrb);

TcpConnection *MpOrb::createConnection(int socketId)
{
    baseConnectionStarted = true;
    auto moduleType = cModuleType::get("mporb.transportlayer.tcp.MpOrbConnection");
    char submoduleName[24];
    snprintf(submoduleName, sizeof(submoduleName), "conn-%d", socketId);
    auto module = check_and_cast<TcpConnection *>(moduleType->createScheduleInit(submoduleName, this));
    module->initConnection(this, socketId);
    mainSocketId = socketId;
    return module;
}

SubflowConnection *MpOrb::createSubflowConnection(int socketId, MpTcpConnection *metaConn, bool isMaster)
{
    auto moduleType = cModuleType::get("mporb.transportlayer.tcp.MpOrbSubflowConnection");
    char submoduleName[24];
    snprintf(submoduleName, sizeof(submoduleName), "conn-%d", socketId);
    auto module = check_and_cast<SubflowConnection *>(moduleType->createScheduleInit(submoduleName, this));
    module->initSubflowConnection(this, socketId, metaConn, isMaster);
    tcpAppConnMap[socketId] = module;
    return module;
}

} // namespace tcp
} // namespace inet
