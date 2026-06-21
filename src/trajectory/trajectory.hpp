#pragma once

#include <Geode/Geode.hpp>
#include <unordered_set>

class Trajectory {
public:
  PlayerObject *m_fakePlayer1 = nullptr;
  PlayerObject *m_fakePlayer2 = nullptr;

  std::unordered_set<uintptr_t> m_activatedObjectsP1;
  std::unordered_set<uintptr_t> m_activatedObjectsP2;

  bool isFakePlayer(cocos2d::CCObject *player);
  PlayerObject *getOtherPlayer(PlayerObject *player);
  void rememberActivatedObject(cocos2d::CCObject *obj, PlayerObject *player);
  bool playerHasActivated(PlayerObject *player, cocos2d::CCObject *obj);
  bool realPlayerHasActivated(PlayerObject *player, cocos2d::CCObject *obj);
  void deactivateAllRemembered();
  bool drawing() { return false; }

  struct UnsafeInner {
    PlayerObject *m_fakePlayer1 = nullptr;
    PlayerObject *m_fakePlayer2 = nullptr;
  };
  UnsafeInner *unsafeInner() {
    static UnsafeInner ui;
    return &ui;
  }
};
