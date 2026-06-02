#pragma once

#include "trace_protocol.h"
#include <ostream>

bool IsValidTraceEvent(const TraceEvent& event, size_t bytesReceived);
void RenderTraceEventText(std::ostream& output, const TraceEvent& event);
void RenderTraceEventJsonl(std::ostream& output, const TraceEvent& event);
