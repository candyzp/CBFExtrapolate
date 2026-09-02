#include "timestamp.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
bool g_performanceDebug = false;

struct PerfStats {
  double totalMs = 0.0;
  double worstMs = 0.0;
  unsigned int samples = 0;

  void add(double ms) {
    if (!std::isfinite(ms) || ms < 0.0)
      return;
    totalMs += ms;
    worstMs = std::max(worstMs, ms);
    ++samples;
  }

  void flush() {
    if (samples == 0)
      return;
    log::info("[CBF PERF] visit avg {:.3f} ms | worst {:.3f} ms | {} frames",
              totalMs / static_cast<double>(samples), worstMs, samples);
    totalMs = 0.0;
    worstMs = 0.0;
    samples = 0;
  }
};

PerfStats g_stats;

$on_mod(Loaded) {
  g_performanceDebug =
      Mod::get()->getSettingValue<bool>("performance-debug");
  listenForSettingChanges<bool>("performance-debug", [](bool value) {
    if (!value)
      g_stats.flush();
    g_performanceDebug = value;
  });
}
} // namespace

class $modify(CBFPerformanceDebugLayer, GJBaseGameLayer) {
  static void onModify(auto &self) {
    // Wrap the normal visit chain so this measures the complete render-side
    // cost while enabled, including CBF prediction and state restoration.
    (void)self.setHookPriority("GJBaseGameLayer::visit", Priority::First);
  }

  void visit() override {
    if (!g_performanceDebug) {
      GJBaseGameLayer::visit();
      return;
    }

    const double start = getCurrentTimestamp();
    GJBaseGameLayer::visit();
    const double elapsedMs = (getCurrentTimestamp() - start) * 1000.0;

    g_stats.add(elapsedMs);

    // Log obvious hitches immediately. The rolling report is intentionally
    // infrequent so the profiler does not become the thing causing the spike.
    if (elapsedMs >= 4.0) {
      log::warn("[CBF PERF] render spike {:.3f} ms", elapsedMs);
    }
    if (g_stats.samples >= 120) {
      g_stats.flush();
    }
  }
};
