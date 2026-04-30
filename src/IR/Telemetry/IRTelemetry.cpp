//===--- IRTelemetry.cpp - IR Telemetry Implementation ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TelemetryCollector class for performance profiling.
//
//===----------------------------------------------------------------------===//

#include "blocktype/IR/IRTelemetry.h"
#include <chrono>
#include <fstream>

namespace blocktype {
namespace telemetry {

// ============================================================
// 辅助函数（前向定义）
// ============================================================

static const char* getPhaseName(CompilationPhase P) {
  switch (P) {
    case CompilationPhase::Frontend:       return "Frontend";
    case CompilationPhase::IRGeneration:   return "IRGeneration";
    case CompilationPhase::IRValidation:   return "IRValidation";
    case CompilationPhase::IROptimization: return "IROptimization";
    case CompilationPhase::BackendCodegen: return "BackendCodegen";
    case CompilationPhase::BackendOptimize: return "BackendOptimize";
    case CompilationPhase::CodeEmission:   return "CodeEmission";
    case CompilationPhase::Linking:        return "Linking";
  }
  return "Unknown";
}

// ============================================================
// PhaseGuard 实现
// ============================================================

TelemetryCollector::PhaseGuard::PhaseGuard(TelemetryCollector& C, CompilationPhase P, StringRef D)
  : Collector_(&C), Phase(P), Detail(D.str()),
    StartNs(getCurrentTimeNs()),
    MemoryBefore(getCurrentMemoryUsage()) {
}

TelemetryCollector::PhaseGuard::~PhaseGuard() {
  if (MovedFrom_ || !Collector_) return;
  
  if (Collector_->Enabled) {
    CompilationEvent E;
    E.Phase = Phase;
    E.Detail = Detail;
    E.StartNs = StartNs;
    E.EndNs = getCurrentTimeNs();
    E.MemoryBefore = MemoryBefore;
    E.MemoryAfter = getCurrentMemoryUsage();
    E.Success = !Failed;
    Collector_->Events.push_back(std::move(E));
  }
}

// ============================================================
// TelemetryCollector 实现
// ============================================================

uint64_t TelemetryCollector::getCurrentTimeNs() {
  auto now = std::chrono::high_resolution_clock::now();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    now.time_since_epoch()
  );
  return ns.count();
}

size_t TelemetryCollector::getCurrentMemoryUsage() {
  // 简化实现：返回 0
  // 实际实现应使用平台特定 API（如 /proc/self/status）
  return 0;
}

bool TelemetryCollector::writeChromeTrace(StringRef Path) const {
  std::ofstream File(Path.str());
  if (!File.is_open()) return false;
  
  File << "{\n";
  File << "  \"traceEvents\": [\n";
  
  for (size_t i = 0; i < Events.size(); ++i) {
    const auto& E = Events[i];
    File << "    {\"ph\": \"B\", \"pid\": 0, \"tid\": 0, "
         << "\"ts\": " << E.StartNs << ", "
         << "\"name\": \"" << getPhaseName(E.Phase) << "\", "
         << "\"cat\": \"compiler\", "
         << "\"args\": {\"detail\": \"" << E.Detail << "\"}}";
    
    if (i < Events.size() - 1) File << ",";
    File << "\n";
  }
  
  File << "  ],\n";
  File << "  \"displayTimeUnit\": \"us\"\n";
  File << "}\n";
  
  return true;
}

bool TelemetryCollector::writeJSONReport(StringRef Path) const {
  std::ofstream File(Path.str());
  if (!File.is_open()) return false;
  
  File << "{\n";
  File << "  \"compilation_phases\": [\n";
  
  for (size_t i = 0; i < Events.size(); ++i) {
    const auto& E = Events[i];
    File << "    {\n";
    File << "      \"name\": \"" << getPhaseName(E.Phase) << "\",\n";
    File << "      \"detail\": \"" << E.Detail << "\",\n";
    File << "      \"duration_ms\": " << (E.EndNs - E.StartNs) / 1000000.0 << ",\n";
    File << "      \"memory_before_mb\": " << E.MemoryBefore / (1024.0 * 1024.0) << ",\n";
    File << "      \"memory_after_mb\": " << E.MemoryAfter / (1024.0 * 1024.0) << ",\n";
    File << "      \"success\": " << (E.Success ? "true" : "false") << "\n";
    File << "    }";
    
    if (i < Events.size() - 1) File << ",";
    File << "\n";
  }
  
  File << "  ]\n";
  File << "}\n";
  
  return true;
}

} // namespace telemetry
} // namespace blocktype
