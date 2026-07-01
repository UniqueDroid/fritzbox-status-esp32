#pragma once

// High-level fetch/parse interface for FRITZ!Box API endpoints.
#include "globals.h"

// Parse gateway endpoint payload and update WAN state globals.
// Returns true when a usable gateway object was found.
bool parseGateway(const String &json);

// Poll gateway status endpoint and refresh WAN health values.
void fetchGatewayStatus();

// Poll interface counters and push calculated RX/TX samples.
void fetchWanTraffic();

// Poll system endpoint and update CPU, memory and temperature metrics.
void fetchSystemStatus();
