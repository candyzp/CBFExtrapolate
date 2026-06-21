#include "bot.hpp"
#include "../physics/object.hpp"
#include "../trajectory/trajectory.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

Trajectory &Bot::trajectory() {
  static Trajectory t;
  return t;
}

bool Trajectory::isFakePlayer(cocos2d::CCObject *player) {
  if (!player)
    return false;
  return player == m_fakePlayer1 || player == m_fakePlayer2;
}

PlayerObject *Trajectory::getOtherPlayer(PlayerObject *player) {
  if (player == m_fakePlayer1)
    return m_fakePlayer2;
  if (player == m_fakePlayer2)
    return m_fakePlayer1;
  return nullptr;
}

void Trajectory::rememberActivatedObject(cocos2d::CCObject *obj,
                                         PlayerObject *player) {
  if (player == m_fakePlayer1) {
    m_activatedObjectsP1.insert(reinterpret_cast<uintptr_t>(obj));
  } else if (player == m_fakePlayer2) {
    m_activatedObjectsP2.insert(reinterpret_cast<uintptr_t>(obj));
  }
}

bool Trajectory::playerHasActivated(PlayerObject *player,
                                    cocos2d::CCObject *obj) {
  if (!obj)
    return false;
  auto effectObj = geode::cast::typeinfo_cast<EffectGameObject *>(obj);
  if (effectObj && effectObj->m_isMultiActivate)
    return false;

  if (player == m_fakePlayer1) {
    return m_activatedObjectsP1.count(reinterpret_cast<uintptr_t>(obj)) > 0;
  } else if (player == m_fakePlayer2) {
    return m_activatedObjectsP2.count(reinterpret_cast<uintptr_t>(obj)) > 0;
  }
  return false;
}

bool Trajectory::realPlayerHasActivated(PlayerObject *player,
                                        cocos2d::CCObject *obj) {
  if (!obj)
    return false;
  auto effectObj = geode::cast::typeinfo_cast<EnhancedGameObject *>(obj);
  if (!effectObj)
    return false;

  auto pl = GJBaseGameLayer::get();
  if (!pl)
    return false;

  if (!isFakePlayer(player)) {
    return phys::hasBeenActivatedByPlayer(player, effectObj);
  }

  PlayerObject *realPlayer =
      (player == m_fakePlayer1) ? pl->m_player1 : pl->m_player2;
  if (!realPlayer)
    return false;

  return phys::hasBeenActivatedByPlayer(realPlayer, effectObj);
}

void Trajectory::deactivateAllRemembered() {
  m_activatedObjectsP1.clear();
  m_activatedObjectsP2.clear();
}
