#pragma once

#include <string>

#include "Status.h"

// Snapshot plus system information (firmware, WiFi, MQTT, memory), as served by
// GET /api/status and published to <base>/status.
std::string statusJson(const cc::Snapshot& snapshot);
