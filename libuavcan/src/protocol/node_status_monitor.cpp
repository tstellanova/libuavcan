/*
 * Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <uavcan/debug.hpp>
#include <uavcan/fatal_error.hpp>
#include <uavcan/protocol/node_status_monitor.hpp>

namespace uavcan
{

NodeStatusMonitor::Entry& NodeStatusMonitor::getEntry(NodeID node_id) const
{
    if (node_id.get() < 1 || node_id.get() > NodeID::Max)
    {
        handleFatalError("NodeStatusMonitor NodeID out of range");
    }
    return entries_[node_id.get() - 1];
}

void NodeStatusMonitor::changeNodeStatus(const NodeID node_id, const Entry new_entry_value)
{
    Entry& entry = getEntry(node_id);
    if (entry.status_code != new_entry_value.status_code)
    {
        NodeStatusChangeEvent event;
        event.node_id = node_id;

        event.old_status.known = entry.time_since_last_update_ms100 >= 0;
        event.old_status.status_code = entry.status_code;

        event.status.known = true;
        event.status.status_code = new_entry_value.status_code;

        UAVCAN_TRACE("NodeStatusMonitor", "Node %i [%s] status change: %i --> %i", int(node_id.get()),
                     (event.old_status.known ? "known" : "new"),
                     int(event.old_status.status_code), int(event.status.status_code));
        handleNodeStatusChange(event);
    }
    entry = new_entry_value;
}

void NodeStatusMonitor::handleNodeStatus(const ReceivedDataStructure<protocol::NodeStatus>& msg)
{
    Entry new_entry_value;
    new_entry_value.time_since_last_update_ms100 = 0;
    new_entry_value.status_code = msg.status_code;

    changeNodeStatus(msg.getSrcNodeID(), new_entry_value);

    handleNodeStatusMessage(msg);
}

void NodeStatusMonitor::handleTimerEvent(const TimerEvent&)
{
    enum { OfflineTimeoutMs100 = protocol::NodeStatus::OFFLINE_TIMEOUT_MS / 100 };

    for (int i = 1; i <= NodeID::Max; i++)
    {
        const NodeID nid(i);
        assert(nid.isUnicast());
        Entry& entry = getEntry(nid);
        if (entry.time_since_last_update_ms100 >= 0 &&
            entry.status_code != protocol::NodeStatus::STATUS_OFFLINE)
        {
            entry.time_since_last_update_ms100 += TimerPeriodMs100;
            if (entry.time_since_last_update_ms100 >= OfflineTimeoutMs100)
            {
                Entry new_entry_value;
                new_entry_value.time_since_last_update_ms100 = OfflineTimeoutMs100;
                new_entry_value.status_code = protocol::NodeStatus::STATUS_OFFLINE;
                changeNodeStatus(nid, new_entry_value);
            }
        }
    }
}

int NodeStatusMonitor::start()
{
    const int res = sub_.start(NodeStatusCallback(this, &NodeStatusMonitor::handleNodeStatus));
    if (res >= 0)
    {
        Timer::startPeriodic(MonotonicDuration::fromMSec(TimerPeriodMs100 * 100));
    }
    return res;
}

void NodeStatusMonitor::forgetNode(NodeID node_id)
{
    if (node_id.isValid())
    {
        Entry& entry = getEntry(node_id);
        entry = Entry();
    }
    else
    {
        assert(0);
    }
}

NodeStatusMonitor::NodeStatus NodeStatusMonitor::getNodeStatus(NodeID node_id) const
{
    if (!node_id.isValid())
    {
        assert(0);
        return NodeStatus();
    }
    NodeStatus status;
    const Entry& entry = getEntry(node_id);
    if (entry.time_since_last_update_ms100 >= 0)
    {
        status.known = true;
        status.status_code = entry.status_code;
    }
    return status;
}

NodeID NodeStatusMonitor::findNodeWithWorstStatus() const
{
    NodeID nid_with_worst_status;
    NodeStatusCode worst_status_code = protocol::NodeStatus::STATUS_OK;

    for (int i = 1; i <= NodeID::Max; i++)
    {
        const NodeID nid(i);
        assert(nid.isUnicast());
        const Entry& entry = getEntry(nid);
        if (entry.time_since_last_update_ms100 >= 0)
        {
            if (entry.status_code > worst_status_code || !nid_with_worst_status.isValid())
            {
                nid_with_worst_status = nid;
                worst_status_code = entry.status_code;
            }
        }
    }
    return nid_with_worst_status;
}

}