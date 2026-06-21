#pragma once

#include <Geode/Geode.hpp>
#include <unordered_set>

class Trajectory {
public:
  bool isFakePlayer(cocos2d::CCObject *player);
  PlayerObject *getOtherPlayer(PlayerObject *player);
  void rememberActivatedObject(cocos2d::CCObject *obj, PlayerObject *player) {}
  bool playerHasActivated(PlayerObject *player, cocos2d::CCObject *obj) {
    return false;
  }
  bool realPlayerHasActivated(PlayerObject *player, cocos2d::CCObject *obj) {
    return false;
  }
  bool drawing() { return false; }

  struct UnsafeInner {
    PlayerObject *m_fakePlayer2 = nullptr;
  };
  UnsafeInner *unsafeInner() {
    static UnsafeInner ui;
    return &ui;
  }
};
