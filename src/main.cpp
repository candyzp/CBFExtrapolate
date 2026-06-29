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
#include <vector>

using namespace geode::prelude;

static bool g_softToggle = false;
static bool g_extrapolating = false;

static bool g_cbfSoftToggle = false;

$on_mod(Loaded) {
  g_softToggle = Mod::get()->getSettingValue<bool>("soft-toggle");
  listenForSettingChanges<bool>("soft-toggle",
                                [](bool value) { g_softToggle = value; });

  if (auto m = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
    g_cbfSoftToggle = m->getSettingValue<bool>("soft-toggle");
    listenForSettingChanges<bool>(
        "soft-toggle", [](bool value) { g_cbfSoftToggle = value; }, m);
  }
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
  fake->m_position = real->m_position;

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
  struct CameraState {
    bool cameraFlip;
    float cameraWidthOffset;
    float cameraHeightOffset;
    float cameraUnzoomedHeightOffset;
    float targetCameraHeightOffset;
    bool calculateTargetHeightOffset;
    bool staticCameraShake;
    bool skipCameraShake;
    float cameraWidth;
    float cameraHeight;
    float cameraUnzoomedX;
    float halfCameraWidth;
    float unk31f8;
    bool unk322a;
    cocos2d::CCPoint cameraPosition;
    cocos2d::CCPoint cameraOffset;
    float cameraZoom;
    float cameraAngle;
  };

  CameraState saveCameraState() {
    CameraState state;
    state.cameraFlip = m_cameraFlip;
    state.cameraWidthOffset = m_cameraWidthOffset;
    state.cameraHeightOffset = m_cameraHeightOffset;
    state.cameraUnzoomedHeightOffset = m_cameraUnzoomedHeightOffset;
    state.targetCameraHeightOffset = m_targetCameraHeightOffset;
    state.calculateTargetHeightOffset = m_calculateTargetHeightOffset;
    state.staticCameraShake = m_staticCameraShake;
    state.skipCameraShake = m_skipCameraShake;
    state.cameraWidth = m_cameraWidth;
    state.cameraHeight = m_cameraHeight;
    state.cameraUnzoomedX = m_cameraUnzoomedX;
    state.halfCameraWidth = m_halfCameraWidth;
    state.unk31f8 = m_unk31f8;
    state.unk322a = m_unk322a;
    state.cameraPosition = m_gameState.m_cameraPosition;
    state.cameraOffset = m_gameState.m_cameraOffset;
    state.cameraZoom = m_gameState.m_cameraZoom;
    state.cameraAngle = m_gameState.m_cameraAngle;
    return state;
  }

  void restoreCameraState(const CameraState &state) {
    m_cameraFlip = state.cameraFlip;
    m_cameraWidthOffset = state.cameraWidthOffset;
    m_cameraHeightOffset = state.cameraHeightOffset;
    m_cameraUnzoomedHeightOffset = state.cameraUnzoomedHeightOffset;
    m_targetCameraHeightOffset = state.targetCameraHeightOffset;
    m_calculateTargetHeightOffset = state.calculateTargetHeightOffset;
    m_staticCameraShake = state.staticCameraShake;
    m_skipCameraShake = state.skipCameraShake;
    m_cameraWidth = state.cameraWidth;
    m_cameraHeight = state.cameraHeight;
    m_cameraUnzoomedX = state.cameraUnzoomedX;
    m_halfCameraWidth = state.halfCameraWidth;
    m_unk31f8 = state.unk31f8;
    m_unk322a = state.unk322a;
    m_gameState.m_cameraPosition = state.cameraPosition;
    m_gameState.m_cameraOffset = state.cameraOffset;
    m_gameState.m_cameraZoom = state.cameraZoom;
    m_gameState.m_cameraAngle = state.cameraAngle;
  }

  struct GroundState {
    float x;
    float y;
    float scaleX;
    float scaleY;
    float rotation;
    float offset;
    float unk;
  };

  GroundState saveGroundState(GJGroundLayer *ground) {
    GroundState state = {0};
    if (ground) {
      state.x = ground->getPositionX();
      state.y = ground->getPositionY();
      state.scaleX = ground->getScaleX();
      state.scaleY = ground->getScaleY();
      state.rotation = ground->getRotation();
      state.offset = ground->m_ground1Offset;
      state.unk = ground->m_unk1cc;
    }
    return state;
  }

  void restoreGroundState(GJGroundLayer *ground, const GroundState &state) {
    if (ground) {
      ground->setPositionX(state.x);
      ground->setPositionY(state.y);
      ground->setScaleX(state.scaleX);
      ground->setScaleY(state.scaleY);
      ground->setRotation(state.rotation);
      ground->m_ground1Offset = state.offset;
      ground->m_unk1cc = state.unk;
    }
  }

  struct Fields {
    PlayerState p1;
    PlayerState p2;
    PlayerObject *m_fakePlayer1 = nullptr;
    PlayerObject *m_fakePlayer2 = nullptr;
    bool m_enableSolidCollisions = true;

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
    if (g_softToggle) {
      GJBaseGameLayer::flipGravity(player, gravity, unk);
      return;
    }
    if (isFakePlayer(player)) {
      phys::flipGravity(player, gravity);
    } else {
      GJBaseGameLayer::flipGravity(player, gravity, unk);
    }
  }

  void teleportPlayer(TeleportPortalObject *obj, PlayerObject *player) {
    if (g_softToggle) {
      GJBaseGameLayer::teleportPlayer(obj, player);
      return;
    }
    if (isFakePlayer(player)) {
      phys::teleportPlayer(this, obj, player);
    } else {
      GJBaseGameLayer::teleportPlayer(obj, player);
    }
  }

  void collisionCheckObjects(PlayerObject *player,
                             gd::vector<GameObject *> *objects, int length,
                             float dt) {
    if (g_softToggle) {
      GJBaseGameLayer::collisionCheckObjects(player, objects, length, dt);
      return;
    }
    if (isFakePlayer(player)) {
      phys::collisionCheckObjects(this, player, objects, length, dt,
                                  m_fields->m_enableSolidCollisions);
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

    GJBaseGameLayer::update(dt);

    for (int i = 0; i < 2; ++i) {
      auto &state = (i == 0) ? m_fields->p1 : m_fields->p2;
      auto player = (i == 0) ? m_player1 : m_player2;
      if (player) {
        if (state.steps > 0) {
          state.tickTime = state.lastDt;
          state.lastSteps = state.steps;
        } else {
          state.lastSteps = 0;
        }
      }
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

    bool flipping = playLayer->isFlipping();

    if (g_softToggle || paused || isPlatformer || flipping) {
      GJBaseGameLayer::visit();
      return;
    }

    auto origTweenActions = m_gameState.m_tweenActions;
    auto origCameraOffset = m_gameState.m_cameraOffset;
    auto origCameraZoom = m_gameState.m_cameraZoom;
    auto origCameraAngle = m_gameState.m_cameraAngle;
    auto origCameraPosition = m_gameState.m_cameraPosition;
    bool origResetActiveObjects = m_resetActiveObjects;

    bool hasCBF = !g_cbfSoftToggle;

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
    CCPoint origP1Rob = {0, 0};
    int origTrailCount1 = 0;
    CCPoint origCurrentPoint1 = {0, 0};
    bool hasTrail1 = false;
    bool simulatedP1 = false;

    CCPoint origP2 = {0, 0};
    CCPoint origP2Rob = {0, 0};
    int origTrailCount2 = 0;
    CCPoint origCurrentPoint2 = {0, 0};
    bool hasTrail2 = false;
    bool simulatedP2 = false;

    CCPoint origObj = {0, 0};
    CCPoint camOff = {0, 0};
    bool hasObj = m_objectLayer != nullptr;

    float origObjScaleX = m_objectLayer ? m_objectLayer->getScaleX() : 1.f;
    float origObjScaleY = m_objectLayer ? m_objectLayer->getScaleY() : 1.f;
    float origP1ScaleX = m_player1 ? m_player1->getScaleX() : 1.f;
    float origP1ScaleY = m_player1 ? m_player1->getScaleY() : 1.f;
    float origP2ScaleX = m_player2 ? m_player2->getScaleX() : 1.f;
    float origP2ScaleY = m_player2 ? m_player2->getScaleY() : 1.f;
    float origGroundScaleX = m_groundLayer ? m_groundLayer->getScaleX() : 1.f;
    float origGroundScaleY = m_groundLayer ? m_groundLayer->getScaleY() : 1.f;
    float origGround2ScaleX =
        m_groundLayer2 ? m_groundLayer2->getScaleX() : 1.f;
    float origGround2ScaleY =
        m_groundLayer2 ? m_groundLayer2->getScaleY() : 1.f;
    float origGroundX = m_groundLayer ? m_groundLayer->getPositionX() : 0.f;
    float origGroundY = m_groundLayer ? m_groundLayer->getPositionY() : 0.f;
    float origGround2X = m_groundLayer2 ? m_groundLayer2->getPositionX() : 0.f;
    float origGround2Y = m_groundLayer2 ? m_groundLayer2->getPositionY() : 0.f;
    GroundState groundState1 = saveGroundState(m_groundLayer);
    GroundState groundState2 = saveGroundState(m_groundLayer2);

    float origObjRot = m_objectLayer ? m_objectLayer->getRotation() : 0.f;
    float origGroundRot = m_groundLayer ? m_groundLayer->getRotation() : 0.f;
    float origGround2Rot = m_groundLayer2 ? m_groundLayer2->getRotation() : 0.f;

    if (hasObj) {
      origObj = m_objectLayer->getPosition();
    }

    CCPoint origBgPos = {0, 0};
    float origBgScaleX = 1.0f;
    float origBgScaleY = 1.0f;
    float origBgRot = 0.0f;
    bool hasBg = m_background != nullptr;
    if (hasBg) {
      origBgPos = m_background->getPosition();
      origBgScaleX = m_background->getScaleX();
      origBgScaleY = m_background->getScaleY();
      origBgRot = m_background->getRotation();
    }

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
            double accumulated = 0.0;

            while (remaining > 0.0) {
              double currentStep = std::min(remaining, stepSize);
              float delta = static_cast<float>(currentStep);

              player->m_playEffects = false;
              player->update(delta);

              accumulated += currentStep;
              if (accumulated >= 0.25) {
                m_fields->m_enableSolidCollisions = true;
                accumulated = 0.0;
              } else {
                m_fields->m_enableSolidCollisions = false;
              }

              if (this->checkCollisions(player, delta, false) == 1) {
                state.isDead = true;
                break;
              }
              phys::checkSpawnObjects(this, player);

              player->m_isDead = false;
              remaining -= currentStep;
            }

            m_fields->m_enableSolidCollisions = true;
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
        if (state.prevTime > 0.0001 && state.lastTime > state.prevTime &&
            state.lastDt > 0.0001f) {
          timeScale =
              (state.lastDt / 60.0f) / (state.lastTime - state.prevTime);
        }
        double dtSeconds = tCurrent - state.lastTime;
        if (dtSeconds < 0.0) {
          dtSeconds = 0.0;
        }
        double maxDtSeconds = 0.0;
        if (state.prevTime > 0.0001 && state.lastTime > state.prevTime) {
          maxDtSeconds = state.lastTime - state.prevTime;
        } else {
          maxDtSeconds = (state.lastDt > 0.0001f)
                             ? (state.lastDt / 60.0f / timeScale)
                             : 0.033;
        }
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
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
          origP1Rob = m_player1->m_position;

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

          if (!state.isDead) {
            m_player1->CCNode::setPosition(
                m_fields->m_fakePlayer1->getPosition());
            m_player1->m_position = m_fields->m_fakePlayer1->m_position;
          } else {
            simulatedP1 = false;
          }
        }
      }
    }

    if (hasP2 && m_fields->m_fakePlayer2 && m_gameState.m_isDualMode) {
      auto &state = m_fields->p2;
      if (state.lastTime != 0 && !dead) {
        double tCurrent = getCurrentTimestamp();
        double timeScale = m_gameState.m_timeWarp;
        if (state.prevTime > 0.0001 && state.lastTime > state.prevTime &&
            state.lastDt > 0.0001f) {
          timeScale =
              (state.lastDt / 60.0f) / (state.lastTime - state.prevTime);
        }
        double dtSeconds = tCurrent - state.lastTime;
        if (dtSeconds < 0.0) {
          dtSeconds = 0.0;
        }
        double maxDtSeconds = 0.0;
        if (state.prevTime > 0.0001 && state.lastTime > state.prevTime) {
          maxDtSeconds = state.lastTime - state.prevTime;
        } else {
          maxDtSeconds = (state.lastDt > 0.0001f)
                             ? (state.lastDt / 60.0f / timeScale)
                             : 0.033;
        }
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
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
          origP2Rob = m_player2->m_position;

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

          if (!state.isDead) {
            m_player2->CCNode::setPosition(
                m_fields->m_fakePlayer2->getPosition());
            m_player2->m_position = m_fields->m_fakePlayer2->m_position;
          } else {
            simulatedP2 = false;
          }
        }
      }
    }

    bool cameraExtrapolated = false;
    CameraState camState;

    if (hasObj && !dead && hasP1 && m_fields->p1.lastTime != 0) {
      double tCurrent = getCurrentTimestamp();
      double timeScale = m_gameState.m_timeWarp;
      if (m_fields->p1.prevTime > 0.0001 &&
          m_fields->p1.lastTime > m_fields->p1.prevTime &&
          m_fields->p1.lastDt > 0.0001f) {
        timeScale = (m_fields->p1.lastDt / 60.0f) /
                    (m_fields->p1.lastTime - m_fields->p1.prevTime);
      }
      double dtSeconds = tCurrent - m_fields->p1.lastTime;
      if (dtSeconds < 0.0) {
        dtSeconds = 0.0;
      }
      double maxDtSeconds = 0.0;
      if (m_fields->p1.prevTime > 0.0001 &&
          m_fields->p1.lastTime > m_fields->p1.prevTime) {
        maxDtSeconds = m_fields->p1.lastTime - m_fields->p1.prevTime;
      } else {
        maxDtSeconds = (m_fields->p1.lastDt > 0.0001f)
                           ? (m_fields->p1.lastDt / 60.0f / timeScale)
                           : 0.033;
      }
      if (dtSeconds > maxDtSeconds) {
        dtSeconds = maxDtSeconds;
      }

      if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
        camState = saveCameraState();
        cameraExtrapolated = true;

        double warpedDt = dtSeconds * timeScale;
        float dtFloat = static_cast<float>(warpedDt);

        this->updateCamera(dtFloat);
      }
    }

    GJBaseGameLayer::visit();

    if (cameraExtrapolated) {
      restoreCameraState(camState);

      if (hasObj) {
        m_objectLayer->setPosition(origObj);
        m_objectLayer->setScaleX(origObjScaleX);
        m_objectLayer->setScaleY(origObjScaleY);
        m_objectLayer->setRotation(origObjRot);
      }
    }

    if (hasP1 && simulatedP1) {
      m_player1->CCNode::setPosition(origP1);
      m_player1->m_position = origP1Rob;

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
      m_player2->m_position = origP2Rob;

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


    restoreGroundState(m_groundLayer, groundState1);
    restoreGroundState(m_groundLayer2, groundState2);
    if (hasBg) {
      m_background->setPosition(origBgPos);
      m_background->setScaleX(origBgScaleX);
      m_background->setScaleY(origBgScaleY);
      m_background->setRotation(origBgRot);
    }
    m_playerDied = origPlayerDied;

    m_gameState.m_tweenActions = origTweenActions;
    m_resetActiveObjects = origResetActiveObjects;
    if (!cameraExtrapolated) {
      m_gameState.m_cameraOffset = origCameraOffset;
      m_gameState.m_cameraZoom = origCameraZoom;
      m_gameState.m_cameraAngle = origCameraAngle;
      m_gameState.m_cameraPosition = origCameraPosition;
    }

    m_fields->p1.steps = 0;
    m_fields->p2.steps = 0;
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
    if (g_softToggle) {
      PlayerObject::ringJump(ring, unk);
      return;
    }
    if (isFakePlayer(this)) {
      phys::ringJump(this, ring);
    } else {
      PlayerObject::ringJump(ring, unk);
    }
  }

  void bumpPlayer(float force, int objectType, bool playEffect,
                  GameObject *object) {
    if (g_softToggle) {
      PlayerObject::bumpPlayer(force, objectType, playEffect, object);
      return;
    }
    if (isFakePlayer(this)) {
      phys::bumpPlayer(this, force, objectType, playEffect, object);
    } else {
      PlayerObject::bumpPlayer(force, objectType, playEffect, object);
    }
  }

  void propellPlayer(float force, bool dontPlayEffect, int objectType) {
    if (g_softToggle) {
      PlayerObject::propellPlayer(force, dontPlayEffect, objectType);
      return;
    }
    if (isFakePlayer(this)) {
      phys::propellPlayer(this, force, dontPlayEffect, objectType);
    } else {
      PlayerObject::propellPlayer(force, dontPlayEffect, objectType);
    }
  }

  void startDashing(DashRingObject *obj) {
    if (g_softToggle) {
      PlayerObject::startDashing(obj);
      return;
    }
    if (isFakePlayer(this)) {
      phys::startDashing(this, obj);
    } else {
      PlayerObject::startDashing(obj);
    }
  }

#ifdef GEODE_IS_WINDOWS
  void stopDashing() {
    if (g_softToggle) {
      PlayerObject::stopDashing();
      return;
    }
    if (isFakePlayer(this)) {
      phys::stopDashing(this);
    } else {
      PlayerObject::stopDashing();
    }
  }
#endif

  void playDeathEffect() {
    if (g_softToggle) {
      PlayerObject::playDeathEffect();
      return;
    }
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

    if (isFakePlayer(this)) {
      PlayerObject::update(dt);
      this->m_isDead = false;
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
    if (g_softToggle) {
      PlayLayer::destroyPlayer(player, object);
      return;
    }
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
    if (!g_softToggle) {
      resetExtrapolation();
    }
  }

  void resetLevelFromStart() {
    PlayLayer::resetLevelFromStart();
    if (!g_softToggle) {
      resetExtrapolation();
    }
  }

  void delayedResetLevel() {
    PlayLayer::delayedResetLevel();
    if (!g_softToggle) {
      resetExtrapolation();
    }
  }

  void fullReset() {
    PlayLayer::fullReset();
    if (!g_softToggle) {
      resetExtrapolation();
    }
  }
};

class $modify(MyRingObject, RingObject) {
  void spawnCircle() {
    if (g_softToggle) {
      RingObject::spawnCircle();
      return;
    }
    if (g_extrapolating) {
      return;
    }
    RingObject::spawnCircle();
  }
};

class $modify(MyEnhancedGameObject, EnhancedGameObject) {
  void activatedByPlayer(PlayerObject *player) {
    if (g_softToggle) {
      EnhancedGameObject::activatedByPlayer(player);
      return;
    }
    if (isFakePlayer(player)) {
      phys::activateForTrajectory(reinterpret_cast<EffectGameObject *>(this),
                                  player);
    } else {
      EnhancedGameObject::activatedByPlayer(player);
    }
  }
};