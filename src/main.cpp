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
#include <Geode/modify/GJGroundLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

static bool g_softToggle = false;
static bool g_extrapolating = false;
static bool g_cbfSoftToggle = false;

struct ExtrapolationData {
  std::vector<CCPoint> trailPoints;
  std::vector<PlayerButtonCommand> pendingClicks;
  std::vector<PlayerButtonCommand> sortedClicks;
  std::vector<cocos2d::CCNode*> aliveNodes;
  std::vector<cocos2d::CCNode*> aliveNodesMiddleground;

  ExtrapolationData() {
    trailPoints.reserve(1024);
    pendingClicks.reserve(64);
    sortedClicks.reserve(64);
    aliveNodes.reserve(512);
    aliveNodesMiddleground.reserve(256);
  }

  void cleanup() {
    trailPoints.clear();
    pendingClicks.clear();
    sortedClicks.clear();
    aliveNodes.clear();
    aliveNodesMiddleground.clear();
  }
};

static ExtrapolationData g_extrapData;

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

static void extrapolatePushButton(PlayerObject* player, PlayerButton button) {
  player->pushButton(button);
}

static void extrapolateReleaseButton(PlayerObject* player, PlayerButton button) {
  player->releaseButton(button);
}

struct PlayerState {
  CCPoint lastPos = {0, 0};
  CCPoint lastVel = {0, 0};
  CCPoint prevVel = {0, 0};
  float lastRot = 0.f;
  double lastTime = 0.0;
  double prevTime = 0.0;
  float lastDt = 0.f;
  int lastSteps = 0;
  int steps = 0;
  double prog = 0.0;
  double tickTime = 0.0;
  bool isDead = false;
};

static void syncFakePlayer(PlayerObject* fake, PlayerObject* real) {
  if (!fake || !real)
    return;

  fake->copyAttributes(real);

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

  if (fake->m_collisionLogTop)
    fake->m_collisionLogTop->removeAllObjects();
  if (fake->m_collisionLogBottom)
    fake->m_collisionLogBottom->removeAllObjects();
  if (fake->m_collisionLogLeft)
    fake->m_collisionLogLeft->removeAllObjects();
  if (fake->m_collisionLogRight)
    fake->m_collisionLogRight->removeAllObjects();

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
    if (fake->m_waveTrail->m_pointArray->count() > 150) {
      fake->m_waveTrail->m_pointArray->removeAllObjects();
    }
  }
  if (fake->m_regularTrail) {
    fake->m_regularTrail->stopStroke();
  }
  if (fake->m_shipStreak) {
    fake->m_shipStreak->stopStroke();
  }
}

static bool isFakePlayer(PlayerObject* player);

static void cleanUpFakePlayer(PlayerObject*& player) {
  if (!player)
    return;

  auto& trajectory = Bot::get()->trajectory();

  if (trajectory.m_fakePlayer1 == player)
    trajectory.m_fakePlayer1 = nullptr;
  if (trajectory.unsafeInner()->m_fakePlayer1 == player)
    trajectory.unsafeInner()->m_fakePlayer1 = nullptr;
  if (trajectory.m_fakePlayer2 == player)
    trajectory.m_fakePlayer2 = nullptr;
  if (trajectory.unsafeInner()->m_fakePlayer2 == player)
    trajectory.unsafeInner()->m_fakePlayer2 = nullptr;

  player->stopAllActions();
  player->unscheduleAllSelectors();

  if (player->getParent()) {
    player->removeFromParentAndCleanup(true);
  }

  player = nullptr;
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

  struct GroundState {
    float x = 0.f;
    float y = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float rotation = 0.f;
    float offset = 0.f;
    float unk = 0.f;
    bool showGround = true;
    bool showGround1 = true;
    bool showGround2 = true;
    bool cameraRotated = false;
  };

  struct RenderPlayerState {
    CCPoint nodePos = {0, 0};
    CCPoint robPos = {0, 0};
    float positionX = 0.f;
    float positionY = 0.f;
    float unmodifiedPositionX = 0.f;
    float unmodifiedPositionY = 0.f;
    CCPoint lastPosition = {0, 0};
    CCPoint lastPortalPos = {0, 0};
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool flipX = false;
    bool flipY = false;
    bool hasWaveTrail = false;
    int waveTrailCount = 0;
    CCPoint waveTrailCurrentPoint = {0, 0};
  };

  struct SavedNodeState {
    cocos2d::CCNode* node = nullptr;
    cocos2d::CCPoint position = {0, 0};
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    bool visible = true;
    bool hasRGBA = false;
    GLubyte opacity = 255;
    cocos2d::ccColor3B color = {255, 255, 255};
  };

  struct Fields {
    PlayerState p1;
    PlayerState p2;
    PlayerObject* m_fakePlayer1 = nullptr;
    PlayerObject* m_fakePlayer2 = nullptr;
    bool m_enableSolidCollisions = true;
    double m_teleportYOffset = 0.0;

    std::vector<SavedNodeState> m_savedGroundChildren1;
    std::vector<SavedNodeState> m_savedGroundChildren2;
    std::vector<SavedNodeState> m_savedMiddleground;

    ~Fields() {
      cleanUpFakePlayer(m_fakePlayer1);
      cleanUpFakePlayer(m_fakePlayer2);
    }
  };

  static void onModify(auto& self) {
    (void)self.setHookPriority("GJBaseGameLayer::update", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::visit", Priority::VeryLate);
    (void)self.setHookPriority("GJBaseGameLayer::flipGravity", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::teleportPlayer", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::toggleFlipped", Priority::VeryEarly);
  }

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

  void restoreCameraState(const CameraState& state) {
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

  RenderPlayerState saveRenderPlayerState(PlayerObject* player) {
    RenderPlayerState state;
    if (!player)
      return state;

    state.nodePos = player->getPosition();
    state.robPos = player->m_position;
    state.positionX = player->m_positionX;
    state.positionY = player->m_positionY;
    state.unmodifiedPositionX = player->m_unmodifiedPositionX;
    state.unmodifiedPositionY = player->m_unmodifiedPositionY;
    state.lastPosition = player->m_lastPosition;
    state.lastPortalPos = player->m_lastPortalPos;
    state.rotation = player->getRotation();
    state.scaleX = player->getScaleX();
    state.scaleY = player->getScaleY();
    state.flipX = player->isFlipX();
    state.flipY = player->isFlipY();

    state.hasWaveTrail = player->m_waveTrail != nullptr;
    if (state.hasWaveTrail) {
      state.waveTrailCurrentPoint = player->m_waveTrail->m_currentPoint;
      if (player->m_waveTrail->m_pointArray) {
        state.waveTrailCount = player->m_waveTrail->m_pointArray->count();
      }
    }

    return state;
  }

  void applyRenderPlayerStateFromFake(PlayerObject* real, PlayerObject* fake) {
    if (!real || !fake)
      return;

    real->CCNode::setPosition(fake->getPosition());
    real->setRotation(fake->getRotation());
    real->setScaleX(fake->getScaleX());
    real->setScaleY(fake->getScaleY());
    real->setFlipX(fake->isFlipX());
    real->setFlipY(fake->isFlipY());

    real->m_position = fake->m_position;
    real->m_positionX = fake->m_positionX;
    real->m_positionY = fake->m_positionY;
    real->m_unmodifiedPositionX = fake->m_unmodifiedPositionX;
    real->m_unmodifiedPositionY = fake->m_unmodifiedPositionY;
    real->m_lastPosition = fake->m_lastPosition;
    real->m_lastPortalPos = fake->m_lastPortalPos;
  }

  void restoreRenderPlayerState(PlayerObject* player, RenderPlayerState const& state) {
    if (!player)
      return;

    player->CCNode::setPosition(state.nodePos);
    player->setRotation(state.rotation);
    player->setScaleX(state.scaleX);
    player->setScaleY(state.scaleY);
    player->setFlipX(state.flipX);
    player->setFlipY(state.flipY);

    player->m_position = state.robPos;
    player->m_positionX = state.positionX;
    player->m_positionY = state.positionY;
    player->m_unmodifiedPositionX = state.unmodifiedPositionX;
    player->m_unmodifiedPositionY = state.unmodifiedPositionY;
    player->m_lastPosition = state.lastPosition;
    player->m_lastPortalPos = state.lastPortalPos;

    if (state.hasWaveTrail && player->m_waveTrail) {
      player->m_waveTrail->m_currentPoint = state.waveTrailCurrentPoint;
      auto* pointArray = player->m_waveTrail->m_pointArray;
      if (pointArray) {
        int count = pointArray->count();
        if (count > state.waveTrailCount) {
          int diff = count - state.waveTrailCount;
          if (diff > 10 || count > 300) {
            pointArray->removeAllObjects();
          } else {
            while (pointArray->count() > state.waveTrailCount) {
              pointArray->removeLastObject();
            }
          }
        }
      }
      player->m_waveTrail->updateStroke(0.f);
    }
  }

  GroundState saveGroundState(GJGroundLayer* ground) {
    GroundState state;
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

  void restoreGroundState(GJGroundLayer* ground, GroundState const& state) {
    if (!ground)
      return;

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

  void saveRGBAState(cocos2d::CCNode* node, SavedNodeState& state) {
    if (auto* sprite = geode::cast::typeinfo_cast<cocos2d::CCSprite*>(node)) {
      state.hasRGBA = true;
      state.opacity = sprite->getOpacity();
      state.color = sprite->getColor();
      return;
    }

    if (auto* layer = geode::cast::typeinfo_cast<cocos2d::CCLayerRGBA*>(node)) {
      state.hasRGBA = true;
      state.opacity = layer->getOpacity();
      state.color = layer->getColor();
    }
  }

  void restoreRGBAState(cocos2d::CCNode* node, SavedNodeState const& state) {
    if (!state.hasRGBA)
      return;

    if (auto* sprite = geode::cast::typeinfo_cast<cocos2d::CCSprite*>(node)) {
      sprite->setOpacity(state.opacity);
      sprite->setColor(state.color);
      return;
    }

    if (auto* layer = geode::cast::typeinfo_cast<cocos2d::CCLayerRGBA*>(node)) {
      layer->setOpacity(state.opacity);
      layer->setColor(state.color);
    }
  }

  void saveNodePositionsRecursive(cocos2d::CCNode* node, std::vector<SavedNodeState>& saved) {
    if (!node)
      return;

    SavedNodeState state;
    state.node = node;
    state.position = node->getPosition();
    state.rotation = node->getRotation();
    state.scaleX = node->getScaleX();
    state.scaleY = node->getScaleY();
    state.visible = node->isVisible();
    saveRGBAState(node, state);

    saved.push_back(state);

    if (node->getChildren()) {
      for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(node->getChildren())) {
        saveNodePositionsRecursive(child, saved);
      }
    }
  }

  void collectAliveNodesRecursive(cocos2d::CCNode* node, std::vector<cocos2d::CCNode*>& alive) {
    if (!node)
      return;

    alive.push_back(node);

    if (node->getChildren()) {
      for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(node->getChildren())) {
        collectAliveNodesRecursive(child, alive);
      }
    }
  }

  void restoreNodePositions(const std::vector<SavedNodeState>& saved, cocos2d::CCNode* root,
                            std::vector<cocos2d::CCNode*>& aliveBuffer) {
    if (!root)
      return;

    aliveBuffer.clear();
    aliveBuffer.reserve(std::max(aliveBuffer.capacity(), saved.size()));
    collectAliveNodesRecursive(root, aliveBuffer);
    std::sort(aliveBuffer.begin(), aliveBuffer.end());

    for (const auto& state : saved) {
      if (!std::binary_search(aliveBuffer.begin(), aliveBuffer.end(), state.node))
        continue;

      state.node->setPosition(state.position);
      state.node->setRotation(state.rotation);
      state.node->setScaleX(state.scaleX);
      state.node->setScaleY(state.scaleY);
      state.node->setVisible(state.visible);
      restoreRGBAState(state.node, state);
    }
  }

  static double computeSafeTimeScale(PlayerState const& state, float gameTimeWarp) {
    double timeScale = gameTimeWarp;
    if (state.prevTime > 0.0001 && state.lastTime > state.prevTime && state.lastDt > 0.0001f) {
      double diff = state.lastTime - state.prevTime;
      if (diff > 0.001) {
        timeScale = (state.lastDt / 60.0f) / diff;
      }
    }
    if (!std::isfinite(timeScale) || timeScale <= 0.0) {
      timeScale = 1.0;
    }
    return timeScale;
  }

  static double computeClampedDtSeconds(PlayerState const& state, double tCurrent, double timeScale) {
    double dtSeconds = tCurrent - state.lastTime;
    if (dtSeconds < 0.0) {
      dtSeconds = 0.0;
    }

    double frameClamp = (0.25 / 60.0) / timeScale;
    double historyClamp = 0.0;

    if (state.prevTime > 0.0001 && state.lastTime > state.prevTime) {
      historyClamp = state.lastTime - state.prevTime;
    } else if (state.lastDt > 0.0001f) {
      historyClamp = state.lastDt / 60.0f / timeScale;
    } else {
      historyClamp = 0.033;
    }

    double maxDtSeconds = std::min(frameClamp, historyClamp);
    if (!std::isfinite(maxDtSeconds) || maxDtSeconds < 0.0) {
      maxDtSeconds = frameClamp;
    }

    if (dtSeconds > maxDtSeconds) {
      dtSeconds = maxDtSeconds;
    }
    return dtSeconds;
  }

  void flipGravity(PlayerObject* player, bool gravity, bool unk) {
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

  void teleportPlayer(TeleportPortalObject* obj, PlayerObject* player) {
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

  void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects,
                             int length, float dt) {
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
    auto playLayer = geode::cast::typeinfo_cast<PlayLayer*>(this);
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
      auto& state = (i == 0) ? m_fields->p1 : m_fields->p2;
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

  PlayerObject* createFakePlayer(bool isPlayer2) {
    auto player = PlayerObject::create(1, 1, this, this, true);
    if (player) {
      player->setVisible(false);
      player->m_isSecondPlayer = isPlayer2;
      player->m_playEffects = false;

      if (player->m_waveTrail) {
        player->m_waveTrail->setVisible(false);
        if (player->m_waveTrail->m_pointArray) {
          player->m_waveTrail->m_pointArray->removeAllObjects();
        }
      }
      if (player->m_regularTrail) {
        player->m_regularTrail->setVisible(false);
        player->m_regularTrail->stopStroke();
      }
      if (player->m_shipStreak) {
        player->m_shipStreak->setVisible(false);
        player->m_shipStreak->stopStroke();
      }

      this->addChild(player);
    }
    return player;
  }

  void visit() override {
    auto playLayer = geode::cast::typeinfo_cast<PlayLayer*>(this);
    if (!playLayer) {
      GJBaseGameLayer::visit();
      return;
    }

    bool isPlatformer = (m_player1 && m_player1->m_isPlatformer) ||
                        (m_player2 && m_player2->m_isPlatformer);

    bool paused = playLayer->getChildByType<PauseLayer>(0) != nullptr ||
                  CCDirector::sharedDirector()->getRunningScene()->getChildByType<PauseLayer>(0) != nullptr;

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

    bool hasCBF = !g_cbfSoftToggle || m_clickBetweenSteps;

    bool origPlayerDied = m_playerDied;
    bool hasP1 = m_player1 != nullptr;
    bool hasP2 = m_player2 != nullptr;

    if (m_objectLayer) {
      if (hasP1) {
        if (!m_fields->m_fakePlayer1 || m_fields->m_fakePlayer1->getParent() != this) {
          cleanUpFakePlayer(m_fields->m_fakePlayer1);
          m_fields->m_fakePlayer1 = createFakePlayer(false);
        }
        Bot::get()->trajectory().m_fakePlayer1 = m_fields->m_fakePlayer1;
        Bot::get()->trajectory().unsafeInner()->m_fakePlayer1 = m_fields->m_fakePlayer1;
      }
      if (hasP2) {
        if (!m_fields->m_fakePlayer2 || m_fields->m_fakePlayer2->getParent() != this) {
          cleanUpFakePlayer(m_fields->m_fakePlayer2);
          m_fields->m_fakePlayer2 = createFakePlayer(true);
        }
        Bot::get()->trajectory().m_fakePlayer2 = m_fields->m_fakePlayer2;
        Bot::get()->trajectory().unsafeInner()->m_fakePlayer2 = m_fields->m_fakePlayer2;
      }
    }
    Bot::get()->trajectory().deactivateAllRemembered();

    RenderPlayerState origP1State;
    bool savedP1State = false;
    bool simulatedP1 = false;

    RenderPlayerState origP2State;
    bool savedP2State = false;
    bool simulatedP2 = false;

    CCPoint origObj = {0, 0};
    bool hasObj = m_objectLayer != nullptr;

    float origObjScaleX = m_objectLayer ? m_objectLayer->getScaleX() : 1.f;
    float origObjScaleY = m_objectLayer ? m_objectLayer->getScaleY() : 1.f;

    GroundState groundState1 = saveGroundState(m_groundLayer);
    GroundState groundState2 = saveGroundState(m_groundLayer2);

    auto& savedGroundChildren1 = m_fields->m_savedGroundChildren1;
    auto& savedGroundChildren2 = m_fields->m_savedGroundChildren2;
    auto& savedMiddleground = m_fields->m_savedMiddleground;

    savedGroundChildren1.clear();
    savedGroundChildren2.clear();
    savedMiddleground.clear();

    savedGroundChildren1.reserve(128);
    savedGroundChildren2.reserve(128);
    savedMiddleground.reserve(128);

    saveNodePositionsRecursive(m_groundLayer, savedGroundChildren1);
    saveNodePositionsRecursive(m_groundLayer2, savedGroundChildren2);
    saveNodePositionsRecursive(m_middleground, savedMiddleground);

    float origObjRot = m_objectLayer ? m_objectLayer->getRotation() : 0.f;

    if (hasObj) {
      origObj = m_objectLayer->getPosition();
    }

    float origInShaderObjScaleX = m_inShaderObjectLayer ? m_inShaderObjectLayer->getScaleX() : 1.f;
    float origInShaderObjScaleY = m_inShaderObjectLayer ? m_inShaderObjectLayer->getScaleY() : 1.f;
    float origInShaderObjRot = m_inShaderObjectLayer ? m_inShaderObjectLayer->getRotation() : 0.f;
    CCPoint origInShaderObjPos = m_inShaderObjectLayer ? m_inShaderObjectLayer->getPosition() : CCPoint{0, 0};

    float origAboveShaderObjScaleX = m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getScaleX() : 1.f;
    float origAboveShaderObjScaleY = m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getScaleY() : 1.f;
    float origAboveShaderObjRot = m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getRotation() : 0.f;
    CCPoint origAboveShaderObjPos = m_aboveShaderObjectLayer ? m_aboveShaderObjectLayer->getPosition() : CCPoint{0, 0};

    CCPoint origBgPos = {0, 0};
    float origBgScaleX = 1.f;
    float origBgScaleY = 1.f;
    float origBgRot = 0.f;
    bool hasBg = m_background != nullptr;
    if (hasBg) {
      origBgPos = m_background->getPosition();
      origBgScaleX = m_background->getScaleX();
      origBgScaleY = m_background->getScaleY();
      origBgRot = m_background->getRotation();
    }

    bool dead = m_playerDied;

    auto extrapolatePlayer =
        [&](PlayerObject* player, PlayerState& state,
            const std::vector<PlayerButtonCommand>& pendingClicks,
            double tCurrent, double timeScale) {
          double dtSeconds = tCurrent - state.lastTime;
          if (dtSeconds < 0.0)
            dtSeconds = 0.0;

          g_extrapData.sortedClicks = pendingClicks;
          auto& sortedClicks = g_extrapData.sortedClicks;

          if (g_cbfSoftToggle && m_clickBetweenSteps) {
            double stepDuration = (0.25 / 60.0) / timeScale;
            for (auto& cmd : sortedClicks) {
              double elapsed = cmd.m_timestamp - state.lastTime;
              if (elapsed < 0.0)
                elapsed = 0.0;
              int stepIndex = static_cast<int>(elapsed / stepDuration);
              cmd.m_timestamp = state.lastTime + (stepIndex + 0.5) * stepDuration;
            }
          }

          std::sort(sortedClicks.begin(), sortedClicks.end(),
                    [](const PlayerButtonCommand& a, const PlayerButtonCommand& b) {
                      return a.m_timestamp < b.m_timestamp;
                    });

          g_extrapolating = true;

          double currentTime = state.lastTime;
          double targetTime = state.lastTime + dtSeconds;
          state.isDead = false;

          auto updatePlayerSubstepped = [&](double dtFrames) {
            double remaining = dtFrames;
            constexpr double stepSize = 0.25;

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

                if (player->m_lastCollisionLeft > 0 || player->m_lastCollisionRight > 0) {
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

          for (const auto& cmd : sortedClicks) {
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

    try {
      if (hasP1 && m_fields->m_fakePlayer1) {
        auto& state = m_fields->p1;
        if (state.lastTime != 0 && !dead) {
          double tCurrent = getCurrentTimestamp();
          double timeScale = computeSafeTimeScale(state, m_gameState.m_timeWarp);
          double dtSeconds = computeClampedDtSeconds(state, tCurrent, timeScale);
          double tCurrentClamped = state.lastTime + dtSeconds;

          if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
            g_extrapData.pendingClicks.clear();
            if (hasCBF) {
              bool isTwoPlayer = m_levelSettings && m_levelSettings->m_twoPlayerMode;
              for (const auto& cmd : m_queuedButtons) {
                bool isTarget = !cmd.m_isPlayer2 || !isTwoPlayer;
                if (isTarget && cmd.m_timestamp > state.lastTime &&
                    cmd.m_timestamp <= tCurrentClamped) {
                  g_extrapData.pendingClicks.push_back(cmd);
                }
              }
            }

            syncFakePlayer(m_fields->m_fakePlayer1, m_player1);
            origP1State = saveRenderPlayerState(m_player1);
            savedP1State = true;
            simulatedP1 = true;

            extrapolatePlayer(m_fields->m_fakePlayer1, state, g_extrapData.pendingClicks,
                              tCurrentClamped, timeScale);

            if (m_player1->m_stateDartSlide > 0 && !m_player1->m_isOnSlope) {
              auto fakePos = m_fields->m_fakePlayer1->getPosition();
              auto fakeRobPos = m_fields->m_fakePlayer1->m_position;
              fakePos.y = origP1State.nodePos.y;
              fakeRobPos.y = origP1State.robPos.y;
              m_fields->m_fakePlayer1->CCNode::setPosition(fakePos);
              m_fields->m_fakePlayer1->m_position = fakeRobPos;
            }

            applyRenderPlayerStateFromFake(m_player1, m_fields->m_fakePlayer1);
          }
        }
      }

      if (hasP2 && m_fields->m_fakePlayer2 && m_gameState.m_isDualMode) {
        auto& state = m_fields->p2;
        if (state.lastTime != 0 && !dead) {
          double tCurrent = getCurrentTimestamp();
          double timeScale = computeSafeTimeScale(state, m_gameState.m_timeWarp);
          double dtSeconds = computeClampedDtSeconds(state, tCurrent, timeScale);
          double tCurrentClamped = state.lastTime + dtSeconds;

          if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
            g_extrapData.pendingClicks.clear();
            if (hasCBF) {
              bool isTwoPlayer = m_levelSettings && m_levelSettings->m_twoPlayerMode;
              for (const auto& cmd : m_queuedButtons) {
                bool isTarget = cmd.m_isPlayer2 || !isTwoPlayer;
                if (isTarget && cmd.m_timestamp > state.lastTime &&
                    cmd.m_timestamp <= tCurrentClamped) {
                  g_extrapData.pendingClicks.push_back(cmd);
                }
              }
            }

            syncFakePlayer(m_fields->m_fakePlayer2, m_player2);
            origP2State = saveRenderPlayerState(m_player2);
            savedP2State = true;
            simulatedP2 = true;

            extrapolatePlayer(m_fields->m_fakePlayer2, state, g_extrapData.pendingClicks,
                              tCurrentClamped, timeScale);

            if (m_player2->m_stateDartSlide > 0 && !m_player2->m_isOnSlope) {
              auto fakePos = m_fields->m_fakePlayer2->getPosition();
              auto fakeRobPos = m_fields->m_fakePlayer2->m_position;
              fakePos.y = origP2State.nodePos.y;
              fakeRobPos.y = origP2State.robPos.y;
              m_fields->m_fakePlayer2->CCNode::setPosition(fakePos);
              m_fields->m_fakePlayer2->m_position = fakeRobPos;
            }

            applyRenderPlayerStateFromFake(m_player2, m_fields->m_fakePlayer2);
          }
        }
      }
    } catch (...) {
      log::error("Crash prevented in visit extrapolation loop.");
    }

    bool cameraExtrapolated = false;
    CameraState camState;
    GJGameState origGameState;

    if (hasObj && !dead && hasP1 && m_fields->p1.lastTime != 0) {
      double tCurrent = getCurrentTimestamp();
      double timeScale = computeSafeTimeScale(m_fields->p1, m_gameState.m_timeWarp);
      double dtSeconds = computeClampedDtSeconds(m_fields->p1, tCurrent, timeScale);

      if (dtSeconds >= 0.0 && dtSeconds < 2.0) {
        camState = saveCameraState();
        origGameState = m_gameState;
        cameraExtrapolated = true;

        double warpedDt = dtSeconds * timeScale;
        float dtFloat = static_cast<float>(warpedDt);

        gd::unordered_map<int, GJValueTween> filteredTweens;
        for (const auto& [actionID, tween] : m_gameState.m_tweenActions) {
          if (actionID == 1 || actionID == 2 || actionID == 7 ||
              (actionID >= 10 && actionID <= 19) || actionID == 21 || actionID == 22) {
            filteredTweens[actionID] = tween;
          }
        }
        m_gameState.m_tweenActions = filteredTweens;
        m_gameState.updateTweenActions(dtFloat);

        bool tempCalculate = m_calculateTargetHeightOffset;
        m_calculateTargetHeightOffset = false;
        g_extrapolating = true;
        this->updateCamera(dtFloat);
        g_extrapolating = false;
        m_calculateTargetHeightOffset = tempCalculate;
      }
    }

    GJBaseGameLayer::visit();

    if (cameraExtrapolated) {
      restoreCameraState(camState);
      m_gameState = origGameState;

      if (hasObj) {
        m_objectLayer->setPosition(origObj);
        m_objectLayer->setScaleX(origObjScaleX);
        m_objectLayer->setScaleY(origObjScaleY);
        m_objectLayer->setRotation(origObjRot);
      }

      restoreGroundState(m_groundLayer, groundState1);
      restoreGroundState(m_groundLayer2, groundState2);

      restoreNodePositions(savedGroundChildren1, m_groundLayer, g_extrapData.aliveNodes);
      restoreNodePositions(savedGroundChildren2, m_groundLayer2, g_extrapData.aliveNodes);
      restoreNodePositions(savedMiddleground, m_middleground, g_extrapData.aliveNodesMiddleground);

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

    if (hasP1 && simulatedP1 && savedP1State) {
      restoreRenderPlayerState(m_player1, origP1State);
    }
    if (hasP2 && simulatedP2 && savedP2State) {
      restoreRenderPlayerState(m_player2, origP2State);
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

static bool isFakePlayer(PlayerObject* player) {
  return Bot::get()->trajectory().isFakePlayer(player);
}

class $modify(MyPlayer, PlayerObject) {
  static void onModify(auto& self) {
    (void)self.setHookPriority("PlayerObject::update", Priority::VeryEarly);
    (void)self.setHookPriorityPre("PlayerObject::playDeathEffect", Priority::First - 100);
    (void)self.setHookPriority("PlayerObject::ringJump", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::bumpPlayer", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::propellPlayer", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::startDashing", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::spiderTestJumpInternal", Priority::VeryEarly);
#ifdef GEODE_IS_WINDOWS
    (void)self.setHookPriority("PlayerObject::stopDashing", Priority::VeryEarly);
#endif
  }

  void ringJump(RingObject* ring, bool unk) {
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

  void bumpPlayer(float force, int objectType, bool playEffect, GameObject* object) {
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

  void startDashing(DashRingObject* obj) {
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
    MyBGL* myGL = nullptr;
    if (gameLayer && geode::cast::typeinfo_cast<PlayLayer*>(gameLayer)) {
      myGL = static_cast<MyBGL*>(gameLayer);
    }

    if (isFakePlayer(this)) {
      PlayerObject::update(dt);
      this->m_isDead = false;
      return;
    }

    PlayerState* state = nullptr;
    if (myGL) {
      bool isP1 = (this == gameLayer->m_player1);
      state = &(isP1 ? myGL->m_fields->p1 : myGL->m_fields->p2);
    }

    CCPoint posBefore = this->getPosition();
    float rotBefore = this->getRotation();
    CCPoint velBefore =
        CCPoint(static_cast<float>(this->getCurrentXVelocity()),
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
      state->lastVel =
          CCPoint(static_cast<float>(this->getCurrentXVelocity()),
                  static_cast<float>(this->m_yVelocity));
      state->lastDt += dt;
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
      MyBGL* myGL = nullptr;
      if (gameLayer && geode::cast::typeinfo_cast<PlayLayer*>(gameLayer)) {
        myGL = static_cast<MyBGL*>(gameLayer);
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
  static void onModify(auto& self) {
    (void)self.setHookPriority("PlayLayer::init", Priority::VeryEarly);
    (void)self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First - 100);
    (void)self.setHookPriority("PlayLayer::resetLevel", Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::resetLevelFromStart", Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::delayedResetLevel", Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::fullReset", Priority::VeryEarly);
  }

  bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
      return false;
    return true;
  }

  void resetExtrapolation() {
    auto myGL = static_cast<MyBGL*>(static_cast<GJBaseGameLayer*>(this));
    if (myGL) {
      myGL->m_fields->p1 = PlayerState();
      myGL->m_fields->p2 = PlayerState();

      if (m_player1 && m_player1->m_waveTrail && m_player1->m_waveTrail->m_pointArray) {
        m_player1->m_waveTrail->m_pointArray->removeAllObjects();
      }
      if (m_player2 && m_player2->m_waveTrail && m_player2->m_waveTrail->m_pointArray) {
        m_player2->m_waveTrail->m_pointArray->removeAllObjects();
      }
      if (myGL->m_fields->m_fakePlayer1 && myGL->m_fields->m_fakePlayer1->m_waveTrail &&
          myGL->m_fields->m_fakePlayer1->m_waveTrail->m_pointArray) {
        myGL->m_fields->m_fakePlayer1->m_waveTrail->m_pointArray->removeAllObjects();
      }
      if (myGL->m_fields->m_fakePlayer2 && myGL->m_fields->m_fakePlayer2->m_waveTrail &&
          myGL->m_fields->m_fakePlayer2->m_waveTrail->m_pointArray) {
        myGL->m_fields->m_fakePlayer2->m_waveTrail->m_pointArray->removeAllObjects();
      }
    }
    g_extrapData.cleanup();
  }

  void destroyPlayer(PlayerObject* player, GameObject* object) override {
    if (g_softToggle) {
      PlayLayer::destroyPlayer(player, object);
      return;
    }
    auto myGL = static_cast<MyBGL*>(static_cast<GJBaseGameLayer*>(this));
    if (myGL) {
      if (player == myGL->m_fields->m_fakePlayer1)
        return;
      if (player == myGL->m_fields->m_fakePlayer2)
        return;
    }
    PlayLayer::destroyPlayer(player, object);
  }

  void resetLevel() override {
    PlayLayer::resetLevel();
    if (!g_softToggle) {
      try {
        resetExtrapolation();
      } catch (...) {
        log::error("Error during PlayLayer::resetLevel cleanup!");
      }
    }
  }

  void loadFromCheckpoint(CheckpointObject* object) {
    PlayLayer::loadFromCheckpoint(object);
    if (!g_softToggle) {
      try {
        resetExtrapolation();
      } catch (...) {
        log::error("Error during PlayLayer::loadFromCheckpoint cleanup!");
      }
    }
  }

  void resetLevelFromStart() {
    PlayLayer::resetLevelFromStart();
    if (!g_softToggle) {
      try {
        resetExtrapolation();
      } catch (...) {
        log::error("Error during PlayLayer::resetLevelFromStart cleanup!");
      }
    }
  }

  void delayedResetLevel() {
    PlayLayer::delayedResetLevel();
    if (!g_softToggle) {
      try {
        resetExtrapolation();
      } catch (...) {
        log::error("Error during PlayLayer::delayedResetLevel cleanup!");
      }
    }
  }

  void fullReset() {
    PlayLayer::fullReset();
    if (!g_softToggle) {
      try {
        resetExtrapolation();
      } catch (...) {
        log::error("Error during PlayLayer::fullReset cleanup!");
      }
    }
  }
};

class $modify(CBFEditorLayer, LevelEditorLayer) {
  void resetLevel() {
    LevelEditorLayer::resetLevel();
    try {
      g_extrapData.cleanup();
    } catch (...) {
      log::error("Error during LevelEditorLayer::resetLevel cleanup!");
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
  void activatedByPlayer(PlayerObject* player) {
    if (g_softToggle) {
      EnhancedGameObject::activatedByPlayer(player);
      return;
    }
    if (isFakePlayer(player)) {
      phys::activateForTrajectory(reinterpret_cast<EffectGameObject*>(this), player);
    } else {
      EnhancedGameObject::activatedByPlayer(player);
    }
  }
};

class $modify(MyGJGroundLayer, GJGroundLayer) {
  void fadeInGround(float duration) {
    if (g_extrapolating) {
      return;
    }
    GJGroundLayer::fadeInGround(duration);
  }

  void fadeOutGround(float duration) {
    if (g_extrapolating) {
      return;
    }
    GJGroundLayer::fadeOutGround(duration);
  }
};
