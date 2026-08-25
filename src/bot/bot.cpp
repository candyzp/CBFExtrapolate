#include "bot.hpp"
#include "../physics/object.hpp"
#include "../trajectory/trajectory.hpp"
#include <Geode/Geode.hpp>

#include <algorithm>

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

PlayerObject *Trajectory::getRealPlayer(PlayerObject *player) {
  if (!player || !isFakePlayer(player))
    return player;

  auto gameLayer = player->m_gameLayer;
  if (!gameLayer)
    return nullptr;

  return player == m_fakePlayer1 ? gameLayer->m_player1
                                 : gameLayer->m_player2;
}

void Trajectory::rememberActivatedObject(cocos2d::CCObject *obj,
                                         PlayerObject *player) {
  std::vector<uintptr_t> *activated = nullptr;
  if (player == m_fakePlayer1) {
    activated = &m_activatedObjectsP1;
  } else if (player == m_fakePlayer2) {
    activated = &m_activatedObjectsP2;
  }
  if (!activated)
    return;

  const auto key = reinterpret_cast<uintptr_t>(obj);
  if (std::find(activated->begin(), activated->end(), key) ==
      activated->end()) {
    activated->push_back(key);
  }
}

bool Trajectory::playerHasActivated(PlayerObject *player,
                                    cocos2d::CCObject *obj) {
  if (!obj)
    return false;
  auto effectObj = geode::cast::typeinfo_cast<EffectGameObject *>(obj);
  if (effectObj && effectObj->m_isMultiActivate)
    return false;

  const auto key = reinterpret_cast<uintptr_t>(obj);
  if (player == m_fakePlayer1) {
    return std::find(m_activatedObjectsP1.begin(), m_activatedObjectsP1.end(),
                     key) != m_activatedObjectsP1.end();
  } else if (player == m_fakePlayer2) {
    return std::find(m_activatedObjectsP2.begin(), m_activatedObjectsP2.end(),
                     key) != m_activatedObjectsP2.end();
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

  if (!isFakePlayer(player)) {
    return phys::hasBeenActivatedByPlayer(player, effectObj);
  }

  PlayerObject *realPlayer = getRealPlayer(player);
  if (!realPlayer)
    return false;

  return phys::hasBeenActivatedByPlayer(realPlayer, effectObj);
}

void Trajectory::deactivateAllRemembered() {
  m_activatedObjectsP1.clear();
  m_activatedObjectsP2.clear();
}
