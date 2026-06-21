#include "bot.hpp"
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
  auto po = geode::cast::typeinfo_cast<PlayerObject *>(player);
  if (!po || !po->m_gameLayer)
    return false;
  return po != po->m_gameLayer->m_player1 && po != po->m_gameLayer->m_player2;
}

PlayerObject *Trajectory::getOtherPlayer(PlayerObject *player) {
  if (!player || !player->m_gameLayer)
    return nullptr;
  if (player == player->m_gameLayer->m_player1)
    return player->m_gameLayer->m_player2;
  return player->m_gameLayer->m_player1;
}
