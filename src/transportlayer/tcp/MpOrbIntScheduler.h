// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_MPORB_INT_SCHEDULER_H
#define INET_MPORB_INT_SCHEDULER_H

#include "../../../../mptcp/src/transportlayer/tcp/MpTcpPacketScheduler.h"
#include <limits>

namespace inet {
namespace tcp {

// Reuses default burst dispatch and pacing bookkeeping, with INT ranking and
// at most one cwnd of unsent assignments per subflow.
class MpOrbIntScheduler : public MpTcpPacketScheduler
{
  public:
    explicit MpOrbIntScheduler(MpTcpConnection *connection);
    bool usesCwndBoundedScheduling() const override { return true; }
    uint32_t getBoundedAssignmentSpace(SubflowConnection *subflow, uint32_t segmentBytes) const override;
    SubflowConnection *selectRetransmissionSubflow(SubflowConnection *source, uint32_t bytes,
            bool requireIdle = true) const override;

  protected:
    SubflowConnection *selectDefaultSubflow(uint32_t bytes) override;
    struct PathEstimate {
        uint32_t space = 0;
        double score = std::numeric_limits<double>::infinity();
        bool hasRate = false;
        bool freshFeedback = false;
    };
    // referenceBytes is common across candidates.
    PathEstimate evaluatePath(SubflowConnection *subflow, uint32_t referenceBytes) const;
};

} // namespace tcp
} // namespace inet
#endif
