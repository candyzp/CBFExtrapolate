#pragma once

#include <Geode/Geode.hpp>
#include <vector>

class Trajectory {
public:
  Trajectory() {
    m_activatedObjectsP1.reserve(16);
    m_activatedObjectsP2.reserve(16);
  }

  PlayerObject *m_fakePlayer1 = nullptr;
  PlayerObject *m_fakePlayer2 = nullptr;

  // A prediction spans at most a fraction of one physics frame, so it can only
  // activate a handful of objects. A reserved vector avoids the node
  // allocation/free churn caused by clearing unordered_sets every render.
  std::vector<uintptr_t> m_activatedObjectsP1;
  std::vector<uintptr_t> m_activatedObjectsP2;

  bool isFakePlayer(cocos2d::CCObject *player);
  PlayerObject *getOtherPlayer(PlayerObject *player);
  PlayerObject *getRealPlayer(PlayerObject *player);
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
