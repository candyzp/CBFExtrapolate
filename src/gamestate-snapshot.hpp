#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

// every non-trivially-copyable member of GJGameState must be listed here
#define CBFE_GAMESTATE_CONTAINERS(X)                                           \
  X(m_spawnChannelRelated0)                                                    \
  X(m_spawnChannelRelated1)                                                    \
  X(m_tweenActions)                                                            \
  X(m_gameObjectPhysics)                                                       \
  X(m_unkVecFloat1)                                                            \
  X(m_activatedObjectIDs)                                                      \
  X(m_stateObjects)                                                            \
  X(m_unkMapPairGJGameEventIntVectorEventTriggerInstance)                      \
  X(m_unkMapPairGJGameEventIntInt)                                             \
  X(m_enterEffectInstanceVectors)                                              \
  X(m_exitEffectInstanceVectors)                                               \
  X(m_enterChannelMap)                                                         \
  X(m_exitChannelMap)                                                          \
  X(m_moveEffectInstances)                                                     \
  X(m_rotateEffectInstances)                                                   \
  X(m_scaleEffectInstances)                                                    \
  X(m_fadeEffectInstances)                                                     \
  X(m_tintEffectInstances)                                                     \
  X(m_unsortedAreaEffects)                                                     \
  X(m_advanceFollowInstances)                                                  \
  X(m_dynamicMoveActions)                                                      \
  X(m_dynamicRotateActions)                                                    \
  X(m_unkUMap8)                                                                \
  X(m_proximityVolumeRelated)                                                  \
  X(m_songChannelStates)                                                       \
  X(m_songTriggerStateVectors)                                                 \
  X(m_sfxTriggerStates)

class GameStateSnapshot {
public:
  explicit GameStateSnapshot(const GJGameState &src) {
    std::memcpy(m_storage, &src, sizeof(GJGameState));
#define CBFE_COPY(m) std::construct_at(&state()->m, src.m);
    CBFE_GAMESTATE_CONTAINERS(CBFE_COPY)
#undef CBFE_COPY
  }

  ~GameStateSnapshot() {
#define CBFE_DESTROY(m) std::destroy_at(&state()->m);
    CBFE_GAMESTATE_CONTAINERS(CBFE_DESTROY)
#undef CBFE_DESTROY
  }

  GameStateSnapshot(const GameStateSnapshot &) = delete;
  GameStateSnapshot &operator=(const GameStateSnapshot &) = delete;

  void restore(GJGameState &dst) const {
    auto *dstBytes = reinterpret_cast<std::byte *>(&dst);
    size_t pos = 0;
    for (const auto &range : containerRanges()) {
      if (range.offset > pos) {
        std::memcpy(dstBytes + pos, m_storage + pos, range.offset - pos);
      }
      pos = std::max(pos, range.offset + range.size);
    }
    if (pos < sizeof(GJGameState)) {
      std::memcpy(dstBytes + pos, m_storage + pos, sizeof(GJGameState) - pos);
    }
#define CBFE_ASSIGN(m) dst.m = state()->m;
    CBFE_GAMESTATE_CONTAINERS(CBFE_ASSIGN)
#undef CBFE_ASSIGN
  }

private:
  struct Range {
    size_t offset;
    size_t size;
  };

#define CBFE_COUNT(m) +1
  static constexpr size_t kContainerCount =
      0 CBFE_GAMESTATE_CONTAINERS(CBFE_COUNT);
#undef CBFE_COUNT

  const std::array<Range, kContainerCount> &containerRanges() const {
    static const auto ranges = [this] {
      std::array<Range, kContainerCount> out{};
      size_t i = 0;
      auto *base = reinterpret_cast<const std::byte *>(state());
#define CBFE_RANGE(m)                                                          \
  out[i++] = {static_cast<size_t>(                                             \
                  reinterpret_cast<const std::byte *>(&state()->m) - base),    \
              sizeof(state()->m)};
      CBFE_GAMESTATE_CONTAINERS(CBFE_RANGE)
#undef CBFE_RANGE
      std::sort(out.begin(), out.end(),
                [](const Range &a, const Range &b) {
                  return a.offset < b.offset;
                });
      return out;
    }();
    return ranges;
  }

  GJGameState *state() {
    return std::launder(reinterpret_cast<GJGameState *>(m_storage));
  }
  const GJGameState *state() const {
    return std::launder(reinterpret_cast<const GJGameState *>(m_storage));
  }

  alignas(GJGameState) std::byte m_storage[sizeof(GJGameState)];
};
