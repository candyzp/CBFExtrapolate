#pragma once

#include <Geode/Geode.hpp>
#include <cstddef>
#include <cstring>
#include <memory>

// GJGameState can't be copied normally (bindings put a CCPoint in a union),
// so plain fields are copied as raw bytes and the containers are rebuilt.
class GameStateSnapshot {
public:
  explicit GameStateSnapshot(const GJGameState &src) {
    copy(*saved(), src);
  }

  ~GameStateSnapshot() { destroyContainers(*saved()); }

  GameStateSnapshot(const GameStateSnapshot &) = delete;
  GameStateSnapshot &operator=(const GameStateSnapshot &) = delete;

  void restore(GJGameState &dst) const {
    destroyContainers(dst);
    copy(dst, *saved());
  }

private:
  // every container member of GJGameState must be listed here
  template <class F> static void forEachContainer(F &&f) {
    f(&GJGameState::m_spawnChannelRelated0);
    f(&GJGameState::m_spawnChannelRelated1);
    f(&GJGameState::m_tweenActions);
    f(&GJGameState::m_gameObjectPhysics);
    f(&GJGameState::m_unkVecFloat1);
    f(&GJGameState::m_activatedObjectIDs);
    f(&GJGameState::m_stateObjects);
    f(&GJGameState::m_unkMapPairGJGameEventIntVectorEventTriggerInstance);
    f(&GJGameState::m_unkMapPairGJGameEventIntInt);
    f(&GJGameState::m_enterEffectInstanceVectors);
    f(&GJGameState::m_exitEffectInstanceVectors);
    f(&GJGameState::m_enterChannelMap);
    f(&GJGameState::m_exitChannelMap);
    f(&GJGameState::m_moveEffectInstances);
    f(&GJGameState::m_rotateEffectInstances);
    f(&GJGameState::m_scaleEffectInstances);
    f(&GJGameState::m_fadeEffectInstances);
    f(&GJGameState::m_tintEffectInstances);
    f(&GJGameState::m_unsortedAreaEffects);
    f(&GJGameState::m_advanceFollowInstances);
    f(&GJGameState::m_dynamicMoveActions);
    f(&GJGameState::m_dynamicRotateActions);
    f(&GJGameState::m_unkUMap8);
    f(&GJGameState::m_proximityVolumeRelated);
    f(&GJGameState::m_songChannelStates);
    f(&GJGameState::m_songTriggerStateVectors);
    f(&GJGameState::m_sfxTriggerStates);
  }

  // `to` must not hold live containers
  static void copy(GJGameState &to, const GJGameState &from) {
    std::memcpy(static_cast<void *>(&to), &from, sizeof(GJGameState));
    forEachContainer([&](auto member) {
      std::construct_at(&(to.*member), from.*member);
    });
  }

  static void destroyContainers(GJGameState &s) {
    forEachContainer([&](auto member) { std::destroy_at(&(s.*member)); });
  }

  GJGameState *saved() const {
    return std::launder(
        reinterpret_cast<GJGameState *>(const_cast<std::byte *>(m_storage)));
  }

  alignas(GJGameState) std::byte m_storage[sizeof(GJGameState)];
};
