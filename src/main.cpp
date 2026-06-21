#include "bot/bot.hpp"
#include "physics/collisions.hpp"
#include "physics/gjbasegamelayer.hpp"
#include "physics/player.hpp"
#include "timestamp.hpp"
#include "trajectory/trajectory.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/DashRingObject.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/RingObject.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_set>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <Geode/loader/Event.hpp>
#include <Geode/utils/Keyboard.hpp>
#endif

using namespace geode::prelude;

static bool g_softToggle = false;
static bool g_extrapolating = false;

$on_mod(Loaded) {
  g_softToggle = Mod::get()->getSettingValue<bool>("soft-toggle");
  listenForSettingChanges<bool>("soft-toggle",
                                [](bool value) { g_softToggle = value; });
}

static void extrapolatePushButton(PlayerObject *player, PlayerButton button) {
  player->pushButton(button);
}

static void extrapolateReleaseButton(PlayerObject *player,
                                     PlayerButton button) {
  player->releaseButton(button);
}

struct PlayerState {
  CCPoint lastPos = {0, 0};
  CCPoint lastVel = {0, 0};
  CCPoint prevVel = {0, 0};
  float lastRot = 0;
  double lastTime = 0;
  double prevTime = 0;
  float lastDt = 0;
  int lastSteps = 0;
  int steps = 0;
  double prog = 0;
  double tickTime = 0;
  bool isDead = false;
};

static void syncFakePlayer(PlayerObject *fake, PlayerObject *real) {
  if (!fake || !real)
    return;
  fake->copyAttributes(real);

  fake->setPosition(real->getPosition());
  fake->setRotation(real->getRotation());

  fake->m_yVelocity = real->m_yVelocity;
  fake->m_platformerXVelocity = real->m_platformerXVelocity;
  fake->m_xVelocityRelated = real->m_xVelocityRelated;
  fake->m_xVelocityRelated2 = real->m_xVelocityRelated2;
  fake->m_gravity = real->m_gravity;
  fake->m_gravityMod = real->m_gravityMod;
  fake->m_speedMultiplier = real->m_speedMultiplier;
  fake->m_playerSpeed = real->m_playerSpeed;
  fake->m_vehicleSize = real->m_vehicleSize;
  fake->m_isUpsideDown = real->m_isUpsideDown;
  fake->setFlipY(real->isFlipY());
  fake->setFlipX(real->isFlipX());

  fake->m_isOnGround = real->m_isOnGround;
  fake->m_isOnGround2 = real->m_isOnGround2;
  fake->m_isOnGround3 = real->m_isOnGround3;
  fake->m_isOnGround4 = real->m_isOnGround4;
  fake->m_isOnSlope = real->m_isOnSlope;
  fake->m_wasOnSlope = real->m_wasOnSlope;
  fake->m_slopeRotation = real->m_slopeRotation;
  fake->m_slopeAngle = real->m_slopeAngle;
  fake->m_slopeAngleRadians = real->m_slopeAngleRadians;
  fake->m_currentSlope = real->m_currentSlope;
  fake->m_currentPotentialSlope = real->m_currentPotentialSlope;
  fake->m_lastGroundObject = real->m_lastGroundObject;
  fake->m_preLastGroundObject = real->m_preLastGroundObject;
  fake->m_maybeLastGroundObject = real->m_maybeLastGroundObject;
  fake->m_collidingWithSlopeId = real->m_collidingWithSlopeId;
  fake->m_slopeFlipGravityRelated = real->m_slopeFlipGravityRelated;
  fake->m_potentialSlopeMap = real->m_potentialSlopeMap;

  fake->m_lastCollisionBottom = real->m_lastCollisionBottom;
  fake->m_lastCollisionTop = real->m_lastCollisionTop;
  fake->m_lastCollisionLeft = real->m_lastCollisionLeft;
  fake->m_lastCollisionRight = real->m_lastCollisionRight;
  fake->m_collidedTopMinY = real->m_collidedTopMinY;
  fake->m_collidedBottomMaxY = real->m_collidedBottomMaxY;
  fake->m_collidedLeftMaxX = real->m_collidedLeftMaxX;
  fake->m_collidedRightMinX = real->m_collidedRightMinX;

  fake->m_touchedRings = real->m_touchedRings;
  if (fake->m_touchingRings && real->m_touchingRings) {
    fake->m_touchingRings->removeAllObjects();
    for (unsigned int i = 0; i < real->m_touchingRings->count(); i++) {
      fake->m_touchingRings->addObject(real->m_touchingRings->objectAtIndex(i));
    }
  }
  fake->m_touchedRing = real->m_touchedRing;
  fake->m_touchedCustomRing = real->m_touchedCustomRing;
  fake->m_touchedGravityPortal = real->m_touchedGravityPortal;
  fake->m_ringRelatedSet = real->m_ringRelatedSet;
  fake->m_lastActivatedPortal = real->m_lastActivatedPortal;

  fake->m_holdingLeft = real->m_holdingLeft;
  fake->m_holdingRight = real->m_holdingRight;
  fake->m_holdingButtons = real->m_holdingButtons;
  fake->m_jumpBuffered = real->m_jumpBuffered;
  fake->m_wasJumpBuffered = real->m_wasJumpBuffered;
  fake->m_hasEverJumped = real->m_hasEverJumped;
  fake->m_isDashing = real->m_isDashing;
  fake->m_isDead = real->m_isDead;
  fake->m_inputsLocked = real->m_inputsLocked;
  fake->m_totalTime = real->m_totalTime;

  fake->m_isShip = real->m_isShip;
  fake->m_isBird = real->m_isBird;
  fake->m_isBall = real->m_isBall;
  fake->m_isDart = real->m_isDart;
  fake->m_isRobot = real->m_isRobot;
  fake->m_isSpider = real->m_isSpider;
  fake->m_isSwing = real->m_isSwing;
  fake->m_playEffects = false;
}

static bool isFakePlayer(PlayerObject *player);

class $modify(MyBGL, GJBaseGameLayer) {
  struct Fields {
    PlayerState p1;
    PlayerState p2;
    CCPoint lastCam = {0, 0};
    CCPoint prevCam = {0, 0};
    std::vector<std::pair<CCNode *, float>> origGroundX;
    PlayerObject *m_fakePlayer1 = nullptr;
    PlayerObject *m_fakePlayer2 = nullptr;

    ~Fields() {
      if (m_fakePlayer1) {
        if (Bot::get()->trajectory().m_fakePlayer1 == m_fakePlayer1) {
          Bot::get()->trajectory().m_fakePlayer1 = nullptr;
        }
        if (Bot::get()->trajectory().unsafeInner()->m_fakePlayer1 ==
            m_fakePlayer1) {
          Bot::get()->trajectory().unsafeInner()->m_fakePlayer1 = nullptr;
        }
        m_fakePlayer1->release();
      }
      if (m_fakePlayer2) {
        if (Bot::get()->trajectory().m_fakePlayer2 == m_fakePlayer2) {
          Bot::get()->trajectory().m_fakePlayer2 = nullptr;
        }
        if (Bot::get()->trajectory().unsafeInner()->m_fakePlayer2 ==
            m_fakePlayer2) {
          Bot::get()->trajectory().unsafeInner()->m_fakePlayer2 = nullptr;
        }
        m_fakePlayer2->release();
      }
    }
  };

  static void onModify(auto &self) {
    (void)self.setHookPriority("GJBaseGameLayer::update", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::visit", Priority::VeryLate);
    (void)self.setHookPriority("GJBaseGameLayer::flipGravity",
                               Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects",
                               Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::teleportPlayer",
                               Priority::VeryEarly);
  }

  void flipGravity(PlayerObject *player, bool gravity, bool unk) {
    if (isFakePlayer(player)) {
      phys::flipGravity(player, gravity);
    } else {
      GJBaseGameLayer::flipGravity(player, gravity, unk);
    }
  }

  void teleportPlayer(TeleportPortalObject *obj, PlayerObject *player) {
    if (isFakePlayer(player)) {
      phys::teleportPlayer(this, obj, player);
    } else {
      GJBaseGameLayer::teleportPlayer(obj, player);
    }
  }

  void collisionCheckObjects(PlayerObject *player,
                             gd::vector<GameObject *> *objects, int length,
                             float dt) {
    if (isFakePlayer(player)) {
      phys::collisionCheckObjects(this, player, objects, length, dt);
    } else {
      GJBaseGameLayer::collisionCheckObjects(player, objects, length, dt);
    }
  }

  void update(float dt) override {
    auto playLayer = geode::cast::typeinfo_cast<PlayLayer *>(this);
    bool isPlatformer = (m_player1 && m_player1->m_isPlatformer) ||
                        (m_player2 && m_player2->m_isPlatformer);
    if (g_softToggle || !playLayer || isPlatformer) {
      GJBaseGameLayer::update(dt);
      return;
    }

    m_fields->p1.steps = 0;
    m_fields->p2.steps = 0;

    CCPoint camBefore =
        m_objectLayer ? m_objectLayer->getPosition() : CCPoint{0, 0};

    GJBaseGameLayer::update(dt);

    bool ran = false;
    for (int i = 0; i < 2; ++i) {
      auto &state = (i == 0) ? m_fields->p1 : m_fields->p2;
      auto player = (i == 0) ? m_player1 : m_player2;
      if (player) {
        if (state.steps > 0) {
          state.tickTime = state.lastDt;
          state.lastSteps = state.steps;
          ran = true;
        } else {
          state.lastSteps = 0;
        }
      }
    }

    if (ran && m_objectLayer) {
      m_fields->lastCam = m_objectLayer->getPosition();
      m_fields->prevCam = camBefore;
    }
  }

  PlayerObject *createFakePlayer(bool isPlayer2) {
    auto player = PlayerObject::create(1, 1, this, this, true);
    if (player) {
      player->retain();
      player->setVisible(false);
      player->m_isSecondPlayer = isPlayer2;
      player->m_playEffects = false;
      this->addChild(player);
    }
    return player;
  }

  void visit() override {
    auto playLayer = geode::cast::typeinfo_cast<PlayLayer *>(this);
    if (!playLayer) {
      GJBaseGameLayer::visit();
      return;
    }

    bool isPlatformer = (m_player1 && m_player1->m_isPlatformer) ||
                        (m_player2 && m_player2->m_isPlatformer);

    bool paused = playLayer->getChildByType<PauseLayer>(0) != nullptr ||
                  CCDirector::sharedDirector()
                          ->getRunningScene()
                          ->getChildByType<PauseLayer>(0) != nullptr;

    if (g_softToggle || isFlipping() || paused || isPlatformer) {
      GJBaseGameLayer::visit();
      return;
    }

    GJGameState origState = m_gameState;
    EffectManagerState ems;
    bool hasEffectManager = (m_effectManager != nullptr);
    if (hasEffectManager) {
      m_effectManager->saveToState(ems);
    }

    bool hasCBF = false;
    if (auto m = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
      hasCBF = !m->getSettingValue<bool>("soft-toggle");
    }

    bool origPlayerDied = m_playerDied;
    bool hasP1 = m_player1 != nullptr;
    bool hasP2 = m_player2 != nullptr;

    if (m_objectLayer) {
      if (hasP1) {
        if (!m_fields->m_fakePlayer1 ||
            m_fields->m_fakePlayer1->getParent() != this) {
          if (m_fields->m_fakePlayer1) {
            m_fields->m_fakePlayer1->release();
            m_fields->m_fakePlayer1 = nullptr;
          }
          m_fields->m_fakePlayer1 = createFakePlayer(false);
        }
        Bot::get()->trajectory().m_fakePlayer1 = m_fields->m_fakePlayer1;
        Bot::get()->trajectory().unsafeInner()->m_fakePlayer1 =
            m_fields->m_fakePlayer1;
      }
      if (hasP2) {
        if (!m_fields->m_fakePlayer2 ||
            m_fields->m_fakePlayer2->getParent() != this) {
          if (m_fields->m_fakePlayer2) {
            m_fields->m_fakePlayer2->release();
            m_fields->m_fakePlayer2 = nullptr;
          }
          m_fields->m_fakePlayer2 = createFakePlayer(true);
        }
        Bot::get()->trajectory().m_fakePlayer2 = m_fields->m_fakePlayer2;
        Bot::get()->trajectory().unsafeInner()->m_fakePlayer2 =
            m_fields->m_fakePlayer2;
      }
    }
    Bot::get()->trajectory().deactivateAllRemembered();

    CCPoint origP1 = {0, 0};
    float origR1 = 0.0f;
    int origTrailCount1 = 0;
    CCPoint origCurrentPoint1 = {0, 0};
    bool hasTrail1 = false;
    bool simulatedP1 = false;

    CCPoint origP2 = {0, 0};
    float origR2 = 0.0f;
    int origTrailCount2 = 0;
    CCPoint origCurrentPoint2 = {0, 0};
    bool hasTrail2 = false;
    bool simulatedP2 = false;

    CCPoint origObj = {0, 0};
    CCPoint camOff = {0, 0};
    double camPct = 0;
    bool hasObj = m_objectLayer != nullptr;

    if (hasObj)
      origObj = m_objectLayer->getPosition();

    float xSign = (hasObj && m_objectLayer->getScaleX() < 0) ? -1 : 1;
    bool dead = m_playerDied || (m_player1 && m_player1->m_isDead) ||
                (m_player2 && m_player2->m_isDead);

    auto extrapolatePlayer =
        [&](PlayerObject *player, PlayerState &state,
            const std::vector<PlayerButtonCommand> &pendingClicks,
            double tCurrent, double timeScale) {
          double dtSeconds = tCurrent - state.lastTime;
          if (dtSeconds < 0.0)
            dtSeconds = 0.0;

          std::vector<PlayerButtonCommand> sortedClicks = pendingClicks;
          std::sort(
              sortedClicks.begin(), sortedClicks.end(),
              [](const PlayerButtonCommand &a, const PlayerButtonCommand &b) {
                return a.m_timestamp < b.m_timestamp;
              });

          g_extrapolating = true;

          double currentTime = state.lastTime;
          double targetTime = state.lastTime + dtSeconds;
          state.isDead = false;

          auto updatePlayerSubstepped = [&](double dtFrames) {
            if (state.isDead)
              return;
            double remaining = dtFrames;
            double stepSize = 0.25;
            while (remaining > 0.0) {
              double currentStep = std::min(remaining, stepSize);
              float delta = static_cast<float>(currentStep);

              player->m_playEffects = false;
              player->update(delta);

              player->m_unkUnused3 = player->getRotation();
              player->updateRotation(delta);
              player->m_shipRotation = player->getPosition();

              if (this->checkCollisions(player, delta, false) == 1) {
                state.isDead = true;
                break;
              }

              phys::checkSpawnObjects(this, player);
              this->m_effectManager->postCollisionCheck();

              player->m_isDead = false;

              remaining -= currentStep;
            }
          };

          for (const auto &cmd : sortedClicks) {
            if (state.isDead)
              break;
            if (cmd.m_timestamp > currentTime && cmd.m_timestamp < targetTime) {
              double dt = (cmd.m_timestamp - currentTime) * timeScale;
              double dtFrames = dt * 60.0;

              updatePlayerSubstepped(dtFrames);

              currentTime = cmd.m_timestamp;

              if (cmd.m_isPush) {
                extrapolatePushButton(player, cmd.m_button);
              } else {
                extrapolateReleaseButton(player, cmd.m_button);
              }
            }
          }

          if (targetTime > currentTime && !state.isDead) {
            double dt = (targetTime - currentTime) * timeScale;
            double dtFrames = dt * 60.0;

            updatePlayerSubstepped(dtFrames);
          }

          player->m_isDead = false;

          g_extrapolating = false;
        };

    if (hasP1 && m_fields->m_fakePlayer1) {
      auto &state = m_fields->p1;
      if (state.lastTime != 0 && !dead) {
        double tCurrent = getCurrentTimestamp();
        double timeScale = m_gameState.m_timeWarp;
        double dtSeconds = tCurrent - state.lastTime;
        double maxDtSeconds = (state.lastDt > 0.0001f)
                                  ? ((state.lastDt / 60.0f) / timeScale)
                                  : 0.033;
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds > 0.0 && dtSeconds < 2.0) {
          std::vector<PlayerButtonCommand> pendingClicks;
          if (hasCBF) {
            bool isTwoPlayer =
                m_levelSettings && m_levelSettings->m_twoPlayerMode;
            for (const auto &cmd : m_queuedButtons) {
              bool isTarget = !cmd.m_isPlayer2 || !isTwoPlayer;
              if (isTarget && cmd.m_timestamp > state.lastTime &&
                  cmd.m_timestamp <= tCurrentClamped) {
                pendingClicks.push_back(cmd);
              }
            }
          }

          syncFakePlayer(m_fields->m_fakePlayer1, m_player1);

          origP1 = m_player1->getPosition();
          origR1 = m_player1->getRotation();

          hasTrail1 = m_player1->m_waveTrail != nullptr;
          if (hasTrail1) {
            origCurrentPoint1 = m_player1->m_waveTrail->m_currentPoint;
            if (m_player1->m_waveTrail->m_pointArray) {
              origTrailCount1 = m_player1->m_waveTrail->m_pointArray->count();
            }
          }

          simulatedP1 = true;

          extrapolatePlayer(m_fields->m_fakePlayer1, state, pendingClicks,
                            tCurrentClamped, timeScale);

          m_player1->CCNode::setPosition(
              m_fields->m_fakePlayer1->getPosition());
          m_player1->setRotation(m_fields->m_fakePlayer1->getRotation());

          CCPoint camDisp = m_fields->lastCam - m_fields->prevCam;
          if (maxDtSeconds > 0.0001) {
            camPct = dtSeconds / maxDtSeconds;
            if (camPct > 1.0)
              camPct = 1.0;
            if (camPct < 0.0)
              camPct = 0.0;
            camOff = camDisp * static_cast<float>(camPct);
          }
        }
      }
    }

    if (hasP2 && m_fields->m_fakePlayer2 && m_gameState.m_isDualMode) {
      auto &state = m_fields->p2;
      if (state.lastTime != 0 && !dead) {
        double tCurrent = getCurrentTimestamp();
        double timeScale = m_gameState.m_timeWarp;
        double dtSeconds = tCurrent - state.lastTime;
        double maxDtSeconds = (state.lastDt > 0.0001f)
                                  ? ((state.lastDt / 60.0f) / timeScale)
                                  : 0.033;
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds > 0.0 && dtSeconds < 2.0) {
          std::vector<PlayerButtonCommand> pendingClicks;
          if (hasCBF) {
            bool isTwoPlayer =
                m_levelSettings && m_levelSettings->m_twoPlayerMode;
            for (const auto &cmd : m_queuedButtons) {
              bool isTarget = cmd.m_isPlayer2 || !isTwoPlayer;
              if (isTarget && cmd.m_timestamp > state.lastTime &&
                  cmd.m_timestamp <= tCurrentClamped) {
                pendingClicks.push_back(cmd);
              }
            }
          }

          syncFakePlayer(m_fields->m_fakePlayer2, m_player2);

          origP2 = m_player2->getPosition();
          origR2 = m_player2->getRotation();

          hasTrail2 = m_player2->m_waveTrail != nullptr;
          if (hasTrail2) {
            origCurrentPoint2 = m_player2->m_waveTrail->m_currentPoint;
            if (m_player2->m_waveTrail->m_pointArray) {
              origTrailCount2 = m_player2->m_waveTrail->m_pointArray->count();
            }
          }

          simulatedP2 = true;

          extrapolatePlayer(m_fields->m_fakePlayer2, state, pendingClicks,
                            tCurrentClamped, timeScale);

          m_player2->CCNode::setPosition(
              m_fields->m_fakePlayer2->getPosition());
          m_player2->setRotation(m_fields->m_fakePlayer2->getRotation());
        }
      }
    }

    if (hasObj && camOff != CCPoint{0, 0}) {
      m_objectLayer->setPosition(origObj + camOff);
    }

    m_fields->origGroundX.clear();
    auto shiftGround = [&](GJGroundLayer *ground, float shift) {
      if (!ground)
        return;
      for (auto *child : CCArrayExt<CCNode *>(ground->getChildren())) {
        if (geode::cast::typeinfo_cast<CCSpriteBatchNode *>(child)) {
          m_fields->origGroundX.push_back({child, child->getPositionX()});
          child->setPositionX(child->getPositionX() + shift);
        }
      }
    };

    if (!dead && hasP1 && m_fields->p1.lastTime != 0 &&
        camOff != CCPoint{0, 0}) {
      float move = m_fields->lastCam.x - m_fields->prevCam.x;
      float shift = move * xSign * static_cast<float>(camPct);
      shiftGround(m_groundLayer, shift);
      shiftGround(m_groundLayer2, shift);
    }

    GJBaseGameLayer::visit();

    if (hasP1 && simulatedP1) {
      m_player1->CCNode::setPosition(origP1);
      m_player1->setRotation(origR1);

      if (hasTrail1) {
        m_player1->m_waveTrail->m_currentPoint = origCurrentPoint1;
        auto *pointArray = m_player1->m_waveTrail->m_pointArray;
        if (pointArray) {
          while (pointArray->count() > origTrailCount1) {
            pointArray->removeLastObject();
          }
        }
        m_player1->m_waveTrail->updateStroke(0.f);
      }
    }
    if (hasP2 && simulatedP2) {
      m_player2->CCNode::setPosition(origP2);
      m_player2->setRotation(origR2);

      if (hasTrail2) {
        m_player2->m_waveTrail->m_currentPoint = origCurrentPoint2;
        auto *pointArray = m_player2->m_waveTrail->m_pointArray;
        if (pointArray) {
          while (pointArray->count() > origTrailCount2) {
            pointArray->removeLastObject();
          }
        }
        m_player2->m_waveTrail->updateStroke(0.f);
      }
    }
    if (hasObj && camOff != CCPoint{0, 0}) {
      m_objectLayer->setPosition(origObj);
    }
    for (const auto &[node, x] : m_fields->origGroundX) {
      node->setPositionX(x);
    }
    m_playerDied = origPlayerDied;

    m_gameState = origState;
    if (hasEffectManager) {
      m_effectManager->loadFromState(ems);
    }
  }
};

static bool isFakePlayer(PlayerObject *player) {
  return Bot::get()->trajectory().isFakePlayer(player);
}

class $modify(MyPlayer, PlayerObject) {
  static void onModify(auto &self) {
    (void)self.setHookPriority("PlayerObject::update", Priority::VeryEarly);
    (void)self.setHookPriorityPre("PlayerObject::playDeathEffect",
                                  Priority::First - 100);
    (void)self.setHookPriority("PlayerObject::ringJump", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::bumpPlayer", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::propellPlayer",
                               Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::startDashing",
                               Priority::VeryEarly);
#ifdef GEODE_IS_WINDOWS
    (void)self.setHookPriority("PlayerObject::stopDashing",
                               Priority::VeryEarly);
#endif
  }

  void ringJump(RingObject *ring, bool unk) {
    if (isFakePlayer(this)) {
      phys::ringJump(this, ring);
    } else {
      PlayerObject::ringJump(ring, unk);
    }
  }

  void bumpPlayer(float force, int objectType, bool playEffect,
                  GameObject *object) {
    if (isFakePlayer(this)) {
      phys::bumpPlayer(this, force, objectType, playEffect, object);
    } else {
      PlayerObject::bumpPlayer(force, objectType, playEffect, object);
    }
  }

  void propellPlayer(float force, bool dontPlayEffect, int objectType) {
    if (isFakePlayer(this)) {
      phys::propellPlayer(this, force, dontPlayEffect, objectType);
    } else {
      PlayerObject::propellPlayer(force, dontPlayEffect, objectType);
    }
  }

  void startDashing(DashRingObject *obj) {
    if (isFakePlayer(this)) {
      phys::startDashing(this, obj);
    } else {
      PlayerObject::startDashing(obj);
    }
  }

#ifdef GEODE_IS_WINDOWS
  void stopDashing() {
    if (isFakePlayer(this)) {
      phys::stopDashing(this);
    } else {
      PlayerObject::stopDashing();
    }
  }
#endif

  void playDeathEffect() {
    if (g_extrapolating || isFakePlayer(this)) {
      return;
    }
    PlayerObject::playDeathEffect();
  }

  void update(float dt) override {
    if (g_softToggle || m_isPlatformer) {
      PlayerObject::update(dt);
      return;
    }

    auto gameLayer = this->m_gameLayer;
    MyBGL *myGL = nullptr;
    if (gameLayer && geode::cast::typeinfo_cast<PlayLayer *>(gameLayer)) {
      myGL = static_cast<MyBGL *>(gameLayer);
    }

    if (g_extrapolating || isFakePlayer(this)) {
      PlayerObject::update(dt);
      if (isFakePlayer(this)) {
        this->m_isDead = false;
      }
      return;
    }

    PlayerState *state = nullptr;
    if (myGL) {
      bool isP1 = (this == gameLayer->m_player1);
      state = &(isP1 ? myGL->m_fields->p1 : myGL->m_fields->p2);
    }

    CCPoint posBefore = this->getPosition();
    float rotBefore = this->getRotation();
    CCPoint velBefore = CCPoint(static_cast<float>(this->getCurrentXVelocity()),
                                static_cast<float>(this->m_yVelocity));

    if (state) {
      if (state->steps == 0) {
        state->prevTime = state->lastTime;
        state->lastPos = posBefore;
        state->prevVel = velBefore;
        state->lastRot = rotBefore;
        state->lastDt = 0;
      }
    }

    PlayerObject::update(dt);

    if (state) {
      state->lastTime = getCurrentTimestamp();
      state->lastVel = CCPoint(static_cast<float>(this->getCurrentXVelocity()),
                               static_cast<float>(this->m_yVelocity));
      state->lastDt += dt;
      state->steps++;
    }
  }
};

class $modify(MyPlayLayer, PlayLayer) {
  static void onModify(auto &self) {
    (void)self.setHookPriority("PlayLayer::init", Priority::VeryEarly);
    (void)self.setHookPriorityPre("PlayLayer::destroyPlayer",
                                  Priority::First - 100);
    (void)self.setHookPriority("PlayLayer::resetLevel", Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::resetLevelFromStart",
                               Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::delayedResetLevel",
                               Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::fullReset", Priority::VeryEarly);
  }

  bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
      return false;

    return true;
  }

  void resetExtrapolation() {
    auto myGL = static_cast<MyBGL *>(static_cast<GJBaseGameLayer *>(this));
    if (myGL) {
      myGL->m_fields->p1 = PlayerState();
      myGL->m_fields->p2 = PlayerState();
      myGL->m_fields->lastCam = CCPoint(0.f, 0.f);
      myGL->m_fields->prevCam = CCPoint(0.f, 0.f);
      if (myGL->m_fields->m_fakePlayer1) {
        myGL->m_fields->m_fakePlayer1->release();
        myGL->m_fields->m_fakePlayer1 = nullptr;
      }
      if (myGL->m_fields->m_fakePlayer2) {
        myGL->m_fields->m_fakePlayer2->release();
        myGL->m_fields->m_fakePlayer2 = nullptr;
      }
    }
  }

  void destroyPlayer(PlayerObject *player, GameObject *object) override {
    auto myGL = static_cast<MyBGL *>(static_cast<GJBaseGameLayer *>(this));
    if (myGL) {
      if (player == myGL->m_fields->m_fakePlayer1) {
        myGL->m_fields->p1.isDead = true;
        return;
      }
      if (player == myGL->m_fields->m_fakePlayer2) {
        myGL->m_fields->p2.isDead = true;
        return;
      }
    }
    PlayLayer::destroyPlayer(player, object);
  }

  void resetLevel() override {
    PlayLayer::resetLevel();
    resetExtrapolation();
  }

  void resetLevelFromStart() {
    PlayLayer::resetLevelFromStart();
    resetExtrapolation();
  }

  void delayedResetLevel() {
    PlayLayer::delayedResetLevel();
    resetExtrapolation();
  }

  void fullReset() {
    PlayLayer::fullReset();
    resetExtrapolation();
  }
};

class $modify(MyRingObject, RingObject) {
  void spawnCircle() {
    if (g_extrapolating) {
      return;
    }
    RingObject::spawnCircle();
  }
};

class $modify(MyEnhancedGameObject, EnhancedGameObject) {
  void activatedByPlayer(PlayerObject *player) {
    if (isFakePlayer(player)) {
      phys::activateForTrajectory(reinterpret_cast<EffectGameObject *>(this),
                                  player);
    } else {
      EnhancedGameObject::activatedByPlayer(player);
    }
  }
};