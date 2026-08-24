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
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJGroundLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
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
  CCPoint previousStepPos = {0, 0};
  CCPoint previousStepRobPos = {0, 0};
  float lastRot = 0;
  double lastTime = 0;
  double prevTime = 0;
  float lastDt = 0;
  float lastStepDt = 0;
  int lastSteps = 0;
  int steps = 0;
  double prog = 0;
  double tickTime = 0;
  bool isDead = false;
};

template <auto... Members> struct GameStateMemberSnapshot {
  using Values =
      std::tuple<std::remove_cvref_t<decltype(
          std::declval<const GJGameState &>().*Members)>...>;

  Values values{};

  void save(const GJGameState &state) {
    values = Values{state.*Members...};
  }

  void restore(GJGameState &state) const {
    std::apply(
        [&](const auto &...value) { ((state.*Members = value), ...); },
        values);
  }
};

// Prediction only mutates scalar/pointer camera and gameplay state. Keeping a
// fixed-size snapshot of those members avoids copying every trigger map and
// effect vector in GJGameState twice per rendered frame. The tween table and
// activated-object map are isolated separately around updateTweenActions below.
using PredictionGameStateSnapshot = GameStateMemberSnapshot<
    &GJGameState::m_cameraZoom, &GJGameState::m_targetCameraZoom,
    &GJGameState::m_cameraOffset, &GJGameState::m_unkPoint1,
    &GJGameState::m_unkPoint2, &GJGameState::m_unkPoint3,
    &GJGameState::m_unkPoint4, &GJGameState::m_unkPoint5,
    &GJGameState::m_unkPoint6, &GJGameState::m_unkPoint7,
    &GJGameState::m_unkPoint8, &GJGameState::m_unkPoint9,
    &GJGameState::m_unkPoint10, &GJGameState::m_unkPoint11,
    &GJGameState::m_unkPoint12, &GJGameState::m_unkPoint13,
    &GJGameState::m_unkPoint14, &GJGameState::m_unkPoint15,
    &GJGameState::m_unkPoint16, &GJGameState::m_unkPoint17,
    &GJGameState::m_unkPoint18, &GJGameState::m_unkPoint19,
    &GJGameState::m_unkPoint20, &GJGameState::m_unkPoint21,
    &GJGameState::m_unkPoint22, &GJGameState::m_unkPoint23,
    &GJGameState::m_unkPoint24, &GJGameState::m_unkPoint25,
    &GJGameState::m_unkPoint26, &GJGameState::m_unkPoint27,
    &GJGameState::m_unkPoint28, &GJGameState::m_unkPoint29,
    &GJGameState::m_unkBool1, &GJGameState::m_unkInt1,
    &GJGameState::m_unkBool2, &GJGameState::m_unkInt2,
    &GJGameState::m_unkBool3, &GJGameState::m_unkPoint30,
    &GJGameState::m_middleGroundOffsetY, &GJGameState::m_unkInt3,
    &GJGameState::m_unkInt4, &GJGameState::m_unkBool4,
    &GJGameState::m_unkBool5, &GJGameState::m_unkFloat2,
    &GJGameState::m_unkFloat3, &GJGameState::m_unkInt5,
    &GJGameState::m_unkInt6, &GJGameState::m_unkInt7,
    &GJGameState::m_unkInt8, &GJGameState::m_unkInt9,
    &GJGameState::m_unkInt10, &GJGameState::m_unkInt11,
    &GJGameState::m_unkFloat4, &GJGameState::m_unkUint1,
    &GJGameState::m_portalY, &GJGameState::m_unkBool6,
    &GJGameState::m_gravityRelated, &GJGameState::m_unkInt12,
    &GJGameState::m_unkInt13, &GJGameState::m_unkInt14,
    &GJGameState::m_unkInt15, &GJGameState::m_unkBool7,
    &GJGameState::m_isFreeMode, &GJGameState::m_unkBool9,
    &GJGameState::m_unkFloat5, &GJGameState::m_unkFloat6,
    &GJGameState::m_unkFloat7, &GJGameState::m_unkFloat8,
    &GJGameState::m_cameraAngle, &GJGameState::m_targetCameraAngle,
    &GJGameState::m_playerStreakBlend, &GJGameState::m_timeWarp,
    &GJGameState::m_queuedTimeWarp, &GJGameState::m_timeWarpRelated,
    &GJGameState::m_currentChannel, &GJGameState::m_rotateChannel,
    &GJGameState::m_totalTime, &GJGameState::m_levelTime,
    &GJGameState::m_unkDouble3, &GJGameState::m_commandIndex,
    &GJGameState::m_unkUint3, &GJGameState::m_currentProgress,
    &GJGameState::m_unkUint4, &GJGameState::m_unkUint5,
    &GJGameState::m_unkUint6, &GJGameState::m_unkUint7,
    &GJGameState::m_unkUint8, &GJGameState::m_lastActivatedPortal1,
    &GJGameState::m_lastActivatedPortal2, &GJGameState::m_cameraPosition,
    &GJGameState::m_unkBool10, &GJGameState::m_levelFlipping,
    &GJGameState::m_unkBool11, &GJGameState::m_unkBool12,
    &GJGameState::m_isDualMode, &GJGameState::m_unkFloat9,
    &GJGameState::m_cameraEdgeValue0, &GJGameState::m_cameraEdgeValue1,
    &GJGameState::m_cameraEdgeValue2, &GJGameState::m_cameraEdgeValue3,
    &GJGameState::m_unkUint10, &GJGameState::m_unkUint11,
    &GJGameState::m_unkUint12, &GJGameState::m_cameraStepDiff,
    &GJGameState::m_unkFloat10, &GJGameState::m_timeModRelated,
    &GJGameState::m_timeModRelated2, &GJGameState::m_unkUint13,
    &GJGameState::m_unkPoint32, &GJGameState::m_cameraPosition2,
    &GJGameState::m_unkBool20, &GJGameState::m_unkBool21,
    &GJGameState::m_unkBool22, &GJGameState::m_unkUint14,
    &GJGameState::m_unkBool26, &GJGameState::m_cameraShakeEnabled,
    &GJGameState::m_cameraShakeDuration,
    &GJGameState::m_cameraShakeStrength,
    &GJGameState::m_cameraShakeInterval, &GJGameState::m_lastShakeTime,
    &GJGameState::m_unkPoint34, &GJGameState::m_dualRelated,
    &GJGameState::m_unkBool27, &GJGameState::m_unkBool28,
    &GJGameState::m_unkBool29, &GJGameState::m_unkUint17,
    &GJGameState::m_unkBool30, &GJGameState::m_background,
    &GJGameState::m_ground, &GJGameState::m_middleground,
    &GJGameState::m_unkBool31, &GJGameState::m_points,
    &GJGameState::m_unkBool32, &GJGameState::m_pauseCounter,
    &GJGameState::m_pauseBufferTimer>;

static bool isSlidingOnDartBlock(PlayerObject *player) {
  return player && player->m_isDart && player->m_stateDartSlide > 0;
}

static bool extrapolateDartSlideFromConfirmedMotion(
    PlayerObject *player, const PlayerState &state,
    const CCPoint &currentPos, const CCPoint &currentRobPos, double dtSeconds,
    double timeScale, CCPoint &renderPos, CCPoint &renderRobPos) {
  if (!isSlidingOnDartBlock(player) || player->m_isOnSlope ||
      state.lastStepDt <= 0.0f) {
    return false;
  }

  double renderFrames = dtSeconds * timeScale * 60.0;
  if (!std::isfinite(renderFrames)) {
    renderFrames = 0.0;
  }
  float alpha = static_cast<float>(
      std::clamp(renderFrames / state.lastStepDt, 0.0, 1.0));

  // Moving objects only advance on physics ticks. Continue the player's last
  // confirmed contact motion while touching a D block instead of running a
  // fake collision against the object's stale position. Player and camera now
  // remain on the same render timeline without switching to a raw frame.
  renderPos.x =
      currentPos.x + (currentPos.x - state.previousStepPos.x) * alpha;
  renderPos.y =
      currentPos.y + (currentPos.y - state.previousStepPos.y) * alpha;
  renderRobPos.x = currentRobPos.x +
                   (currentRobPos.x - state.previousStepRobPos.x) * alpha;
  renderRobPos.y = currentRobPos.y +
                   (currentRobPos.y - state.previousStepRobPos.y) * alpha;
  return true;
}

static void syncFakePlayer(PlayerObject *fake, PlayerObject *real) {
  if (!fake || !real)
    return;

  fake->setPosition(real->getPosition());
  fake->setRotation(real->getRotation());
  fake->m_position = real->m_position;
  fake->m_positionX = real->m_positionX;
  fake->m_positionY = real->m_positionY;
  fake->m_unmodifiedPositionX = real->m_unmodifiedPositionX;
  fake->m_unmodifiedPositionY = real->m_unmodifiedPositionY;
  fake->m_lastPosition = real->m_lastPosition;
  fake->m_lastPortalPos = real->m_lastPortalPos;

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

  fake->m_collidedObject = real->m_collidedObject;
  fake->m_collidingWithLeft = real->m_collidingWithLeft;
  fake->m_collidingWithRight = real->m_collidingWithRight;
  fake->m_isCollidingWithSlope = real->m_isCollidingWithSlope;
  fake->m_maybeIsColliding = real->m_maybeIsColliding;
  fake->m_maybeTouchedBreakableBlock = real->m_maybeTouchedBreakableBlock;
  fake->m_touchedPad = real->m_touchedPad;
  fake->m_isGoingLeft = real->m_isGoingLeft;
  fake->m_isSideways = real->m_isSideways;

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

  fake->m_stateHitHead = real->m_stateHitHead;
  fake->m_stateDartSlide = real->m_stateDartSlide;
  fake->m_stateNoAutoJump = real->m_stateNoAutoJump;
  fake->m_stateFlipGravity = real->m_stateFlipGravity;
  fake->m_stateForce = real->m_stateForce;
  fake->m_stateForceVector = real->m_stateForceVector;
  fake->m_jumpPadRelated = real->m_jumpPadRelated;

  fake->m_dashX = real->m_dashX;
  fake->m_dashY = real->m_dashY;
  fake->m_dashAngle = real->m_dashAngle;
  fake->m_dashStartTime = real->m_dashStartTime;
  fake->m_dashRing = real->m_dashRing;

  if (fake->m_waveTrail && fake->m_waveTrail->m_pointArray) {
    fake->m_waveTrail->m_pointArray->removeAllObjects();
  }
  if (fake->m_regularTrail) {
    fake->m_regularTrail->stopStroke();
  }
  if (fake->m_shipStreak) {
    fake->m_shipStreak->stopStroke();
  }
}

static bool isFakePlayer(PlayerObject *player);

static void cleanUpFakePlayer(PlayerObject *&player) {
  if (!player)
    return;

  if (Bot::get()->trajectory().m_fakePlayer1 == player) {
    Bot::get()->trajectory().m_fakePlayer1 = nullptr;
  }
  if (Bot::get()->trajectory().unsafeInner()->m_fakePlayer1 == player) {
    Bot::get()->trajectory().unsafeInner()->m_fakePlayer1 = nullptr;
  }
  if (Bot::get()->trajectory().m_fakePlayer2 == player) {
    Bot::get()->trajectory().m_fakePlayer2 = nullptr;
  }
  if (Bot::get()->trajectory().unsafeInner()->m_fakePlayer2 == player) {
    Bot::get()->trajectory().unsafeInner()->m_fakePlayer2 = nullptr;
  }

  player->release();
  player = nullptr;
}

static void resetFakePlayerTransientState(PlayerObject *player) {
  if (!player)
    return;

  player->m_isDead = false;
  player->m_playEffects = false;

  if (player->m_collisionLogTop)
    player->m_collisionLogTop->removeAllObjects();
  if (player->m_collisionLogBottom)
    player->m_collisionLogBottom->removeAllObjects();
  if (player->m_collisionLogLeft)
    player->m_collisionLogLeft->removeAllObjects();
  if (player->m_collisionLogRight)
    player->m_collisionLogRight->removeAllObjects();
  if (player->m_touchingRings)
    player->m_touchingRings->removeAllObjects();
  if (player->m_waveTrail && player->m_waveTrail->m_pointArray)
    player->m_waveTrail->m_pointArray->removeAllObjects();
  if (player->m_regularTrail)
    player->m_regularTrail->stopStroke();
  if (player->m_shipStreak)
    player->m_shipStreak->stopStroke();
}

class $modify(MyBGL, GJBaseGameLayer) {
  struct CameraState {
    float cameraFlip;
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
    bool showGround;
    bool showGround1;
    bool showGround2;
    bool cameraRotated;
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
      state.showGround = ground->m_showGround;
      state.showGround1 = ground->m_showGround1;
      state.showGround2 = ground->m_showGround2;
      state.cameraRotated = ground->m_cameraRotated;
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
      ground->m_showGround = state.showGround;
      ground->m_showGround1 = state.showGround1;
      ground->m_showGround2 = state.showGround2;
      ground->m_cameraRotated = state.cameraRotated;
    }
  }

  struct SavedNodeState {
    cocos2d::CCNode *node;
    cocos2d::CCNode *parent;
    cocos2d::CCRGBAProtocol *rgba;
    cocos2d::CCPoint position;
    float rotation;
    float scaleX;
    float scaleY;
    bool visible;
    unsigned char opacity;
    unsigned int childCount;
  };

  unsigned int nodeChildCount(cocos2d::CCNode *node) {
    auto children = node ? node->getChildren() : nullptr;
    return children ? children->count() : 0;
  }

  void buildNodeStateCache(cocos2d::CCNode *node,
                           std::vector<SavedNodeState> &saved) {
    if (!node)
      return;
    auto rgba = dynamic_cast<cocos2d::CCRGBAProtocol *>(node);
    saved.push_back({node, node->getParent(), rgba, node->getPosition(),
                     node->getRotation(), node->getScaleX(), node->getScaleY(),
                     node->isVisible(),
                     static_cast<unsigned char>(rgba ? rgba->getOpacity() : 255),
                     nodeChildCount(node)});

    node->retain();
    if (node->getChildren()) {
      for (auto *child :
           geode::cocos::CCArrayExt<cocos2d::CCNode *>(node->getChildren())) {
        buildNodeStateCache(child, saved);
      }
    }
  }

  void releaseNodeStates(std::vector<SavedNodeState> &saved) {
    for (const auto &state : saved) {
      state.node->release();
    }
    saved.clear();
  }

  void prepareNodeStates(cocos2d::CCNode *root,
                         std::vector<SavedNodeState> &saved) {
    bool valid = root ? !saved.empty() && saved.front().node == root
                      : saved.empty();

    if (valid) {
      for (auto &state : saved) {
        if (state.node->getParent() != state.parent ||
            nodeChildCount(state.node) != state.childCount) {
          valid = false;
          break;
        }

        state.position = state.node->getPosition();
        state.rotation = state.node->getRotation();
        state.scaleX = state.node->getScaleX();
        state.scaleY = state.node->getScaleY();
        state.visible = state.node->isVisible();
        state.opacity = state.rgba ? state.rgba->getOpacity() : 255;
      }
    }

    if (!valid) {
      releaseNodeStates(saved);
      buildNodeStateCache(root, saved);
    }
  }

  void restoreNodeStates(const std::vector<SavedNodeState> &saved) {
    for (const auto &state : saved) {
      if (state.node->getParent() != state.parent)
        continue;

      auto position = state.node->getPosition();
      if (position.x != state.position.x || position.y != state.position.y)
        state.node->setPosition(state.position);
      if (state.node->getRotation() != state.rotation)
        state.node->setRotation(state.rotation);
      if (state.node->getScaleX() != state.scaleX)
        state.node->setScaleX(state.scaleX);
      if (state.node->getScaleY() != state.scaleY)
        state.node->setScaleY(state.scaleY);
      if (state.node->isVisible() != state.visible)
        state.node->setVisible(state.visible);
      if (state.rgba && state.rgba->getOpacity() != state.opacity)
        state.rgba->setOpacity(state.opacity);
    }
  }

  struct Fields {
    PlayerState p1;
    PlayerState p2;
    PlayerObject *m_fakePlayer1 = nullptr;
    PlayerObject *m_fakePlayer2 = nullptr;
    std::uint64_t m_attemptGeneration = 0;
    bool m_enableSolidCollisions = true;
    double m_teleportYOffset = 0.0;
    PredictionGameStateSnapshot m_gameStateSnapshot;
    gd::map<std::pair<int, int>, int> m_activatedObjectIDsSnapshot;
    std::vector<SavedNodeState> m_savedGroundChildren1;
    std::vector<SavedNodeState> m_savedGroundChildren2;
    std::vector<SavedNodeState> m_savedMiddleground;
    std::vector<PlayerButtonCommand> m_pendingClicks1;
    std::vector<PlayerButtonCommand> m_pendingClicks2;
    gd::unordered_map<int, GJValueTween> m_filteredTweens;
#ifdef GEODE_IS_ANDROID
    gd::unordered_map<int, GJValueTween> m_originalTweens;
#endif

    Fields() {
      m_savedGroundChildren1.reserve(64);
      m_savedGroundChildren2.reserve(64);
      m_savedMiddleground.reserve(64);
      m_pendingClicks1.reserve(8);
      m_pendingClicks2.reserve(8);
      m_filteredTweens.reserve(16);
#ifdef GEODE_IS_ANDROID
      m_originalTweens.reserve(16);
#endif
    }

    ~Fields() {
      for (const auto &state : m_savedGroundChildren1)
        state.node->release();
      for (const auto &state : m_savedGroundChildren2)
        state.node->release();
      for (const auto &state : m_savedMiddleground)
        state.node->release();
      cleanUpFakePlayer(m_fakePlayer1);
      cleanUpFakePlayer(m_fakePlayer2);
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
    (void)self.setHookPriority("GJBaseGameLayer::toggleFlipped",
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
      double yBefore = player->getPositionY();
      phys::teleportPlayer(this, obj, player);
      double yAfter = player->getPositionY();
      m_fields->m_teleportYOffset += (yAfter - yBefore);
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

  void toggleFlipped(bool flip, bool noEffects) {
    if (g_softToggle) {
      GJBaseGameLayer::toggleFlipped(flip, noEffects);
      return;
    }
    if (g_extrapolating) {
      return;
    }
    GJBaseGameLayer::toggleFlipped(flip, noEffects);
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

    auto &trajectory = Bot::get()->trajectory();

    // Let the real death/retry lifecycle own this frame.
    if (m_playerDied) {
      trajectory.deactivateAllRemembered();
      m_fields->p1.steps = 0;
      m_fields->p2.steps = 0;
      GJBaseGameLayer::visit();
      return;
    }

    const std::uint64_t attemptGeneration = m_fields->m_attemptGeneration;

    // Every visual prediction in this visit must target the same instant.
    // Reading the clock again after player simulation made the camera predict
    // farther than the players, with the mismatch varying by frame workload.
    const double renderTime = getCurrentTimestamp();

    // Save every scalar/pointer member that fake physics or camera prediction
    // can touch. Container-heavy trigger/effect state stays in place.
    auto &origGameState = m_fields->m_gameStateSnapshot;
    origGameState.save(m_gameState);
    m_fields->m_activatedObjectIDsSnapshot =
        m_gameState.m_activatedObjectIDs;
    auto origCameraOffset = m_gameState.m_cameraOffset;
    auto origCameraZoom = m_gameState.m_cameraZoom;
    auto origCameraAngle = m_gameState.m_cameraAngle;
    auto origCameraPosition = m_gameState.m_cameraPosition;
    bool origResetActiveObjects = m_resetActiveObjects;

    bool hasCBF = !g_cbfSoftToggle || m_clickBetweenSteps;

    bool origPlayerDied = m_playerDied;
    bool hasP1 = m_player1 != nullptr;
    bool hasP2 = m_player2 != nullptr;
    if (m_objectLayer) {
      if (hasP1) {
        if (!m_fields->m_fakePlayer1 ||
            m_fields->m_fakePlayer1->getParent() != this) {
          cleanUpFakePlayer(m_fields->m_fakePlayer1);
          m_fields->m_fakePlayer1 = createFakePlayer(false);
        }
        trajectory.m_fakePlayer1 = m_fields->m_fakePlayer1;
        trajectory.unsafeInner()->m_fakePlayer1 = m_fields->m_fakePlayer1;
      }
      if (hasP2) {
        if (!m_fields->m_fakePlayer2 ||
            m_fields->m_fakePlayer2->getParent() != this) {
          cleanUpFakePlayer(m_fields->m_fakePlayer2);
          m_fields->m_fakePlayer2 = createFakePlayer(true);
        }
        trajectory.m_fakePlayer2 = m_fields->m_fakePlayer2;
        trajectory.unsafeInner()->m_fakePlayer2 = m_fields->m_fakePlayer2;
      }
    }
    trajectory.deactivateAllRemembered();

    CCPoint origP1 = {0, 0};
    CCPoint origP1Rob = {0, 0};
    bool simulatedP1 = false;

    CCPoint origP2 = {0, 0};
    CCPoint origP2Rob = {0, 0};
    bool simulatedP2 = false;

    CCPoint origObj = {0, 0};
    bool hasObj = m_objectLayer != nullptr;

    float origObjScaleX = m_objectLayer ? m_objectLayer->getScaleX() : 1.f;
    float origObjScaleY = m_objectLayer ? m_objectLayer->getScaleY() : 1.f;
    GroundState groundState1 = saveGroundState(m_groundLayer);
    GroundState groundState2 = saveGroundState(m_groundLayer2);

    auto &savedGroundChildren1 = m_fields->m_savedGroundChildren1;
    auto &savedGroundChildren2 = m_fields->m_savedGroundChildren2;
    auto &savedMiddleground = m_fields->m_savedMiddleground;

    prepareNodeStates(m_groundLayer, savedGroundChildren1);
    prepareNodeStates(m_groundLayer2, savedGroundChildren2);
    prepareNodeStates(m_middleground, savedMiddleground);

    float origObjRot = m_objectLayer ? m_objectLayer->getRotation() : 0.f;
    if (hasObj) {
      origObj = m_objectLayer->getPosition();
    }

    float origInShaderObjScaleX =
        m_inShaderObjectLayer ? m_inShaderObjectLayer->getScaleX() : 1.f;
    float origInShaderObjScaleY =
        m_inShaderObjectLayer ? m_inShaderObjectLayer->getScaleY() : 1.f;
    float origInShaderObjRot =
        m_inShaderObjectLayer ? m_inShaderObjectLayer->getRotation() : 0.f;
    CCPoint origInShaderObjPos = m_inShaderObjectLayer
                                     ? m_inShaderObjectLayer->getPosition()
                                     : CCPoint{0, 0};

    float origAboveShaderObjScaleX =
        m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getScaleX() : 1.f;
    float origAboveShaderObjScaleY =
        m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getScaleY() : 1.f;
    float origAboveShaderObjRot = m_aboveShaderObjectLayer
                                      ? m_aboveShaderObjectLayer->getRotation()
                                      : 0.f;
    CCPoint origAboveShaderObjPos =
        m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getPosition()
                                 : CCPoint{0, 0};

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

    bool dead = m_playerDied;

    auto extrapolatePlayer =
        [&](PlayerObject *player, PlayerState &state,
            std::vector<PlayerButtonCommand> &sortedClicks,
            double tCurrent, double timeScale) {
          double dtSeconds = tCurrent - state.lastTime;
          if (dtSeconds < 0.0)
            dtSeconds = 0.0;

          if (g_cbfSoftToggle && m_clickBetweenSteps) {
            double stepDuration = (0.25 / 60.0) / timeScale;
            for (auto &cmd : sortedClicks) {
              double elapsed = cmd.m_timestamp - state.lastTime;
              if (elapsed < 0.0)
                elapsed = 0.0;
              int stepIndex = static_cast<int>(elapsed / stepDuration);
              cmd.m_timestamp =
                  state.lastTime + (stepIndex + 0.5) * stepDuration;
            }
          }
          if (sortedClicks.size() > 1) {
            std::sort(sortedClicks.begin(), sortedClicks.end(),
                      [](const PlayerButtonCommand &a,
                         const PlayerButtonCommand &b) {
                        return a.m_timestamp < b.m_timestamp;
                      });
          }

          g_extrapolating = true;

          double currentTime = state.lastTime;
          double targetTime = state.lastTime + dtSeconds;
          state.isDead = false;

          auto updatePlayerSubstepped = [&](double dtFrames) {
            double remaining = dtFrames;
            double stepSize = 0.25;

            while (remaining > 0.0) {
              double currentStep = std::min(remaining, stepSize);
              float delta = static_cast<float>(currentStep);

              m_fields->m_enableSolidCollisions = true;

              player->m_playEffects = false;

              if (player->m_collisionLogTop)
                player->m_collisionLogTop->removeAllObjects();
              if (player->m_collisionLogBottom)
                player->m_collisionLogBottom->removeAllObjects();
              if (player->m_collisionLogLeft)
                player->m_collisionLogLeft->removeAllObjects();
              if (player->m_collisionLogRight)
                player->m_collisionLogRight->removeAllObjects();

              int origNoAutoJump = player->m_stateNoAutoJump;
              int origDartSlide = player->m_stateDartSlide;
              int origHitHead = player->m_stateHitHead;
              int origFlipGravity = player->m_stateFlipGravity;

              player->update(delta);

              player->m_stateNoAutoJump = origNoAutoJump;
              player->m_stateDartSlide = origDartSlide;
              player->m_stateHitHead = origHitHead;
              player->m_stateFlipGravity = origFlipGravity;

              float yBefore = player->getPositionY();
              double yVelBefore = player->m_yVelocity;
              m_fields->m_teleportYOffset = 0.0;

              this->checkCollisions(player, delta, true);
              phys::checkSpawnObjects(this, player);
              if (!player->m_isOnSlope && player->m_stateDartSlide <= 0) {
                float yAfter = player->getPositionY();
                float pushOutY = yAfter - yBefore - m_fields->m_teleportYOffset;

                if (player->m_lastCollisionLeft > 0 ||
                    player->m_lastCollisionRight > 0) {
                  if (pushOutY > 0.01f && yVelBefore > 0.05) {
                    float targetY = yBefore + m_fields->m_teleportYOffset;
                    player->setPositionY(targetY);
                    player->m_position.y = targetY;
                    player->m_yVelocity = yVelBefore;
                  } else if (pushOutY < -0.01f && yVelBefore < -0.05) {
                    float targetY = yBefore + m_fields->m_teleportYOffset;
                    player->setPositionY(targetY);
                    player->m_position.y = targetY;
                    player->m_yVelocity = yVelBefore;
                  }
                }
              }

              player->m_isDead = false;
              remaining -= currentStep;
            }

            m_fields->m_enableSolidCollisions = true;
          };

          for (const auto &cmd : sortedClicks) {
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

          if (targetTime > currentTime) {
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
        double tCurrent = renderTime;
        double timeScale = m_gameState.m_timeWarp;
        if (state.prevTime > 0.0001 && state.lastTime > state.prevTime &&
            state.lastDt > 0.0001f) {
          double diff = state.lastTime - state.prevTime;
          if (diff > 0.001) {
            timeScale = (state.lastDt / 60.0f) / diff;
          }
        }
        if (!std::isfinite(timeScale) || timeScale <= 0.0) {
          timeScale = 1.0;
        }
        double dtSeconds = tCurrent - state.lastTime;
        if (dtSeconds < 0.0) {
          dtSeconds = 0.0;
        }
        double maxDtSeconds = (0.25 / 60.0) / timeScale;
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
          origP1 = m_player1->getPosition();
          origP1Rob = m_player1->m_position;

          simulatedP1 = true;

          CCPoint renderPos;
          CCPoint renderRobPos;
          if (!extrapolateDartSlideFromConfirmedMotion(
                  m_player1, state, origP1, origP1Rob, dtSeconds, timeScale,
                  renderPos, renderRobPos)) {
            auto &pendingClicks = m_fields->m_pendingClicks1;
            pendingClicks.clear();
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
            extrapolatePlayer(m_fields->m_fakePlayer1, state, pendingClicks,
                              tCurrentClamped, timeScale);
            renderPos = m_fields->m_fakePlayer1->getPosition();
            renderRobPos = m_fields->m_fakePlayer1->m_position;
          }

          m_player1->CCNode::setPosition(renderPos);
          m_player1->m_position = renderRobPos;
        }
      }
    }

    if (hasP2 && m_fields->m_fakePlayer2 && m_gameState.m_isDualMode) {
      auto &state = m_fields->p2;
      if (state.lastTime != 0 && !dead) {
        double tCurrent = renderTime;
        double timeScale = m_gameState.m_timeWarp;
        if (state.prevTime > 0.0001 && state.lastTime > state.prevTime &&
            state.lastDt > 0.0001f) {
          double diff = state.lastTime - state.prevTime;
          if (diff > 0.001) {
            timeScale = (state.lastDt / 60.0f) / diff;
          }
        }
        if (!std::isfinite(timeScale) || timeScale <= 0.0) {
          timeScale = 1.0;
        }
        double dtSeconds = tCurrent - state.lastTime;
        if (dtSeconds < 0.0) {
          dtSeconds = 0.0;
        }
        double maxDtSeconds = (0.25 / 60.0) / timeScale;
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
          origP2 = m_player2->getPosition();
          origP2Rob = m_player2->m_position;

          simulatedP2 = true;

          CCPoint renderPos;
          CCPoint renderRobPos;
          if (!extrapolateDartSlideFromConfirmedMotion(
                  m_player2, state, origP2, origP2Rob, dtSeconds, timeScale,
                  renderPos, renderRobPos)) {
            auto &pendingClicks = m_fields->m_pendingClicks2;
            pendingClicks.clear();
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
            extrapolatePlayer(m_fields->m_fakePlayer2, state, pendingClicks,
                              tCurrentClamped, timeScale);
            renderPos = m_fields->m_fakePlayer2->getPosition();
            renderRobPos = m_fields->m_fakePlayer2->m_position;
          }

          m_player2->CCNode::setPosition(renderPos);
          m_player2->m_position = renderRobPos;
        }
      }
    }

    bool cameraExtrapolated = false;
    CameraState camState;

    if (hasObj && !dead && hasP1 && m_fields->p1.lastTime != 0) {
      double tCurrent = renderTime;
      double timeScale = m_gameState.m_timeWarp;
      if (m_fields->p1.prevTime > 0.0001 &&
          m_fields->p1.lastTime > m_fields->p1.prevTime &&
          m_fields->p1.lastDt > 0.0001f) {
        double diff = m_fields->p1.lastTime - m_fields->p1.prevTime;
        if (diff > 0.001) {
          timeScale = (m_fields->p1.lastDt / 60.0f) / diff;
        }
      }
      if (!std::isfinite(timeScale) || timeScale <= 0.0) {
        timeScale = 1.0;
      }
      double dtSeconds = tCurrent - m_fields->p1.lastTime;
      if (dtSeconds < 0.0) {
        dtSeconds = 0.0;
      }
      double maxDtSeconds = (0.25 / 60.0) / timeScale;
      if (dtSeconds > maxDtSeconds) {
        dtSeconds = maxDtSeconds;
      }

      if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
        camState = saveCameraState();
        cameraExtrapolated = true;

        double warpedDt = dtSeconds * timeScale;
        float dtFloat = static_cast<float>(warpedDt);

        auto &filteredTweens = m_fields->m_filteredTweens;
        filteredTweens.clear();
        for (const auto &[actionID, tween] : m_gameState.m_tweenActions) {
          if (actionID == 1 || actionID == 2 || actionID == 7 ||
              (actionID >= 10 && actionID <= 19) || actionID == 21 ||
              actionID == 22) {
            filteredTweens[actionID] = tween;
          }
        }
#ifdef GEODE_IS_ANDROID
        auto &originalTweens = m_fields->m_originalTweens;
        originalTweens = m_gameState.m_tweenActions;
        m_gameState.m_tweenActions = filteredTweens;
#else
        // Temporarily install only camera-related tweens. Swapping preserves
        // the full table and its allocation without copying it each frame.
        m_gameState.m_tweenActions.swap(filteredTweens);
#endif

        m_gameState.updateTweenActions(dtFloat);

        g_extrapolating = true;
        this->updateCamera(dtFloat);
        g_extrapolating = false;

        // Restore gameplay containers before other visit hooks can run.
#ifdef GEODE_IS_ANDROID
        m_gameState.m_tweenActions = m_fields->m_originalTweens;
        m_gameState.m_activatedObjectIDs =
            m_fields->m_activatedObjectIDsSnapshot;
#else
        m_gameState.m_tweenActions.swap(m_fields->m_filteredTweens);
        m_gameState.m_activatedObjectIDs.swap(
            m_fields->m_activatedObjectIDsSnapshot);
#endif
      }
    }

    // Only predicted node transforms remain installed for rendering.
    m_playerDied = origPlayerDied;
    m_resetActiveObjects = origResetActiveObjects;

    GJBaseGameLayer::visit();

    // Never restore snapshots from an attempt reset inside another visit hook.
    if (m_fields->m_attemptGeneration != attemptGeneration) {
      releaseNodeStates(savedGroundChildren1);
      releaseNodeStates(savedGroundChildren2);
      releaseNodeStates(savedMiddleground);
      m_fields->m_pendingClicks1.clear();
      m_fields->m_pendingClicks2.clear();
      m_fields->m_filteredTweens.clear();
#ifdef GEODE_IS_ANDROID
      m_fields->m_originalTweens.clear();
#endif
      m_fields->p1.steps = 0;
      m_fields->p2.steps = 0;
      return;
    }

    if (cameraExtrapolated) {
      restoreCameraState(camState);
      origGameState.restore(m_gameState);

      if (hasObj) {
        m_objectLayer->setPosition(origObj);
        m_objectLayer->setScaleX(origObjScaleX);
        m_objectLayer->setScaleY(origObjScaleY);
        m_objectLayer->setRotation(origObjRot);
      }
      restoreGroundState(m_groundLayer, groundState1);
      restoreGroundState(m_groundLayer2, groundState2);

      restoreNodeStates(savedGroundChildren1);
      restoreNodeStates(savedGroundChildren2);
      restoreNodeStates(savedMiddleground);

      if (hasBg) {
        m_background->setPosition(origBgPos);
        m_background->setScaleX(origBgScaleX);
        m_background->setScaleY(origBgScaleY);
        m_background->setRotation(origBgRot);
      }

      if (m_inShaderObjectLayer) {
        m_inShaderObjectLayer->setPosition(origInShaderObjPos);
        m_inShaderObjectLayer->setScaleX(origInShaderObjScaleX);
        m_inShaderObjectLayer->setScaleY(origInShaderObjScaleY);
        m_inShaderObjectLayer->setRotation(origInShaderObjRot);
      }
      if (m_aboveShaderObjectLayer) {
        m_aboveShaderObjectLayer->setPosition(origAboveShaderObjPos);
        m_aboveShaderObjectLayer->setScaleX(origAboveShaderObjScaleX);
        m_aboveShaderObjectLayer->setScaleY(origAboveShaderObjScaleY);
        m_aboveShaderObjectLayer->setRotation(origAboveShaderObjRot);
      }
    }

    if (hasP1 && simulatedP1) {
      m_player1->CCNode::setPosition(origP1);
      m_player1->m_position = origP1Rob;

    }
    if (hasP2 && simulatedP2) {
      m_player2->CCNode::setPosition(origP2);
      m_player2->m_position = origP2Rob;

    }

    if (!cameraExtrapolated) {
      m_gameState.m_cameraOffset = origCameraOffset;
      m_gameState.m_cameraZoom = origCameraZoom;
      m_gameState.m_cameraAngle = origCameraAngle;
      m_gameState.m_cameraPosition = origCameraPosition;
    }

    m_fields->m_filteredTweens.clear();
#ifdef GEODE_IS_ANDROID
    m_fields->m_originalTweens.clear();
#endif

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
    (void)self.setHookPriority("PlayerObject::spiderTestJumpInternal",
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
    CCPoint robPosBefore = this->m_position;
    float rotBefore = this->getRotation();
    CCPoint velBefore = CCPoint(static_cast<float>(this->getCurrentXVelocity()),
                                static_cast<float>(this->m_yVelocity));

    if (state) {
      state->previousStepPos = posBefore;
      state->previousStepRobPos = robPosBefore;
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
      state->lastStepDt = dt;
      state->steps++;
    }
  }

  void spiderTestJumpInternal(bool dynamic) {
    if (g_softToggle) {
      PlayerObject::spiderTestJumpInternal(dynamic);
      return;
    }
    if (isFakePlayer(this)) {
      double yBefore = this->getPositionY();
      PlayerObject::spiderTestJumpInternal(dynamic);
      double yAfter = this->getPositionY();
      auto gameLayer = this->m_gameLayer;
      MyBGL *myGL = nullptr;
      if (gameLayer && geode::cast::typeinfo_cast<PlayLayer *>(gameLayer)) {
        myGL = static_cast<MyBGL *>(gameLayer);
      }
      if (myGL) {
        myGL->m_fields->m_teleportYOffset += (yAfter - yBefore);
      }
    } else {
      PlayerObject::spiderTestJumpInternal(dynamic);
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
      auto fields = myGL->m_fields.self();
      ++fields->m_attemptGeneration;
      fields->p1 = PlayerState();
      fields->p2 = PlayerState();
      fields->m_enableSolidCollisions = true;
      fields->m_teleportYOffset = 0.0;
      fields->m_pendingClicks1.clear();
      fields->m_pendingClicks2.clear();
      fields->m_activatedObjectIDsSnapshot.clear();
      fields->m_filteredTweens.clear();
#ifdef GEODE_IS_ANDROID
      fields->m_originalTweens.clear();
#endif

      resetFakePlayerTransientState(fields->m_fakePlayer1);
      resetFakePlayerTransientState(fields->m_fakePlayer2);
      Bot::get()->trajectory().deactivateAllRemembered();
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
        return;
      }
      if (player == myGL->m_fields->m_fakePlayer2) {
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

  void loadFromCheckpoint(CheckpointObject *object) {
    PlayLayer::loadFromCheckpoint(object);
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
