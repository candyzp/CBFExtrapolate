#include "timestamp.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

struct Click {
  int button;
  bool push;
  double time;
};

struct PlayerState {
  CCPoint lastPos = {0, 0};
  CCPoint prevPos = {0, 0};
  CCPoint lastVel = {0, 0};
  CCPoint prevVel = {0, 0};
  float lastRot = 0;
  double lastTime = 0;
  double prevTime = 0;
  float lastDt = 0;
  int lastSteps = 0;
  int steps = 0;
  std::vector<Click> inputs;
  std::vector<Click> physInputs;
  double prog = 0;
  double tickTime = 0;
};

class $modify(MyBGL, GJBaseGameLayer) {
  struct Fields {
    PlayerState p1;
    PlayerState p2;
    CCPoint lastCam = {0, 0};
    CCPoint prevCam = {0, 0};
    CCPoint prevCam2 = {0, 0};
    std::vector<std::pair<GameObject *, CCPoint>> movingObjects;
    std::unordered_set<GameObject *> movingObjectsSet;
    bool m_inVisit = false;

    struct Mov {
      CCPoint lastPos = {0, 0};
      CCPoint corrPos = {0, 0};
      bool corrX = false;
      bool corrY = false;
    };
    std::unordered_map<GameObject *, Mov> persists;

    ~Fields() {
      for (auto const &[obj, pos] : movingObjects) {
        obj->release();
      }
      for (auto const &[obj, m] : persists) {
        obj->release();
      }
    }
  };

  static void onModify(auto &self) {
    (void)self.setHookPriority("GJBaseGameLayer::update", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::visit", Priority::VeryLate);
  }

  void update(float dt) override {
    for (auto const &[obj, pos] : m_fields->movingObjects) {
      obj->release();
    }
    m_fields->movingObjects.clear();
    m_fields->movingObjectsSet.clear();

    for (const auto &cmd : m_queuedButtons) {
      auto &state = cmd.m_isPlayer2 ? m_fields->p2 : m_fields->p1;
      state.inputs.push_back({.button = static_cast<int>(cmd.m_button),
                              .push = cmd.m_isPush,
                              .time = cmd.m_timestamp});
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
          state.prog = 0;
          state.lastSteps = state.steps;
        } else {
          if (dt != 0) {
            state.prog += static_cast<double>(dt) * 60;
          }
          state.lastSteps = 0;
        }
      }
    }
  }

  void visit() override {
    if (isFlipping()) {
      GJBaseGameLayer::visit();
      return;
    }

    bool ran = (m_fields->p1.steps > 0 || m_fields->p2.steps > 0);
    if (ran && m_objectLayer) {
      m_fields->prevCam2 = m_fields->prevCam;
      m_fields->prevCam = m_fields->lastCam;
      m_fields->lastCam = m_objectLayer->getPosition();
    }

    m_fields->m_inVisit = true;

    CCPoint origP1 = {0, 0};
    CCPoint origP2 = {0, 0};
    float origR1 = 0;
    float origR2 = 0;
    bool hasP1 = m_player1 != nullptr;
    bool hasP2 = m_player2 != nullptr;

    CCPoint origObj = {0, 0};
    CCPoint camOff = {0, 0};
    double camPct = 0;
    bool hasObj = m_objectLayer != nullptr;

    if (hasObj)
      origObj = m_objectLayer->getPosition();

    static auto *cbfMod =
        Loader::get()->getLoadedMod("syzzi.click_between_frames");
    static bool cachedHasCBF = false;
    static double lastCheckTime = 0;
    double now = getCurrentTimestamp();
    if (now - lastCheckTime > 0.5) {
      cachedHasCBF = cbfMod && !cbfMod->getSettingValue<bool>("soft-toggle");
      lastCheckTime = now;
    }
    bool hasCBF = cachedHasCBF;
    float xSign = (hasObj && m_objectLayer->getScaleX() < 0) ? -1 : 1;
    bool dead = m_playerDied;

    if (hasP1) {
      auto &state = m_fields->p1;
      if (state.lastTime != 0) {
        double pct = 0;
        if (!dead && state.tickTime > 0) {
          pct = state.prog / state.tickTime;
        }
        if (pct > 1)
          pct = 1;
        if (pct < 0)
          pct = 0;

        camPct = pct;

        CCPoint disp = m_player1->getPosition() - state.lastPos;
        CCPoint extOff = disp * static_cast<float>(pct);

        CCPoint clickOffs = {0, 0};
        if (hasCBF) {
          for (const auto &click : state.physInputs) {
            float dy = state.lastVel.y - state.prevVel.y;
            float dx = state.lastVel.x - state.prevVel.x;
            double tRel = click.time - state.prevTime;
            if (tRel < 0)
              tRel = 0;
            if (tRel > state.lastDt)
              tRel = state.lastDt;

            double clkPct = tRel / state.lastDt;
            if (pct < clkPct) {
              clickOffs.x -= dx * (state.lastDt - tRel) * pct * 60;
              clickOffs.y -= dy * (state.lastDt - tRel) * pct * 60;
            } else {
              clickOffs.x -= dx * tRel * (1 - pct) * 60;
              clickOffs.y -= dy * tRel * (1 - pct) * 60;
            }
          }
        }

        CCPoint totalOff = extOff + clickOffs;

        float rCur = m_player1->getRotation();
        float rLast = state.lastRot;
        float rDisp = rCur - rLast;
        if (rDisp > 180)
          rDisp -= 360;
        if (rDisp < -180)
          rDisp += 360;
        float extRot = rDisp * static_cast<float>(pct);

        origP1 = m_player1->getPosition();
        origR1 = m_player1->getRotation();

        m_player1->CCNode::setPosition(origP1 + totalOff);
        m_player1->setRotation(origR1 + extRot);

        CCPoint camDisp = m_fields->lastCam - m_fields->prevCam;
        camOff = dead ? CCPoint{0, 0} : camDisp * static_cast<float>(pct);
      }
    }

    if (hasP2) {
      auto &state = m_fields->p2;
      if (state.lastTime != 0) {
        double pct = 0;
        if (!dead && state.tickTime > 0) {
          pct = state.prog / state.tickTime;
        }
        if (pct > 1)
          pct = 1;
        if (pct < 0)
          pct = 0;

        CCPoint disp = m_player2->getPosition() - state.lastPos;
        CCPoint extOff = disp * static_cast<float>(pct);

        CCPoint clickOffs = {0, 0};
        if (hasCBF) {
          for (const auto &click : state.physInputs) {
            float dy = state.lastVel.y - state.prevVel.y;
            float dx = state.lastVel.x - state.prevVel.x;
            double tRel = click.time - state.prevTime;
            if (tRel < 0)
              tRel = 0;
            if (tRel > state.lastDt)
              tRel = state.lastDt;

            double clkPct = tRel / state.lastDt;
            if (pct < clkPct) {
              clickOffs.x -= dx * (state.lastDt - tRel) * pct * 60;
              clickOffs.y -= dy * (state.lastDt - tRel) * pct * 60;
            } else {
              clickOffs.x -= dx * tRel * (1 - pct) * 60;
              clickOffs.y -= dy * tRel * (1 - pct) * 60;
            }
          }
        }

        CCPoint totalOff = extOff + clickOffs;

        float rCur = m_player2->getRotation();
        float rLast = state.lastRot;
        float rDisp = rCur - rLast;
        if (rDisp > 180)
          rDisp -= 360;
        if (rDisp < -180)
          rDisp += 360;
        float extRot = rDisp * static_cast<float>(pct);

        origP2 = m_player2->getPosition();
        origR2 = m_player2->getRotation();

        m_player2->CCNode::setPosition(origP2 + totalOff);
        m_player2->setRotation(origR2 + extRot);
      }
    }

    if (hasObj && camOff != CCPoint{0, 0}) {
      m_objectLayer->setPosition(m_fields->lastCam + camOff);
    }

    std::vector<std::pair<CCNode *, float>> origGroundX;
    auto shiftGround = [&](GJGroundLayer *ground, float shift) {
      if (!ground)
        return;
      for (auto *child : CCArrayExt<CCNode *>(ground->getChildren())) {
        if (geode::cast::typeinfo_cast<CCSpriteBatchNode *>(child)) {
          origGroundX.push_back({child, child->getPositionX()});
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

    CCPoint dp1_prev = {0, 0};
    CCPoint dp1_curr = {0, 0};
    if (hasP1 && m_fields->p1.lastTime != 0) {
      dp1_prev = m_fields->p1.lastPos - m_fields->p1.prevPos;
      dp1_curr = m_player1->getPosition() - m_fields->p1.lastPos;
    }

    CCPoint dp2_prev = {0, 0};
    CCPoint dp2_curr = {0, 0};
    if (hasP2 && m_fields->p2.lastTime != 0) {
      dp2_prev = m_fields->p2.lastPos - m_fields->p2.prevPos;
      dp2_curr = m_player2->getPosition() - m_fields->p2.lastPos;
    }

    CCPoint dcam_prev = m_fields->prevCam - m_fields->prevCam2;
    CCPoint dcam_curr = m_fields->lastCam - m_fields->prevCam;

    auto isCloseTo = [](float val, float target, float eps = 0.02f) {
      return std::abs(val - target) < eps;
    };

    auto isValidFollowRatio = [isCloseTo](float ratio) {
      return isCloseTo(ratio, 1.0f) || isCloseTo(ratio, -1.0f) ||
             isCloseTo(ratio, 0.5f) || isCloseTo(ratio, -0.5f) ||
             isCloseTo(ratio, 2.0f) || isCloseTo(ratio, -2.0f);
    };

    if (dead) {
      for (auto const &[obj, m] : m_fields->persists) {
        obj->release();
      }
      m_fields->persists.clear();
    }

    if (ran && !dead && hasP1 && m_fields->p1.lastTime != 0) {
      std::unordered_set<GameObject *> current;
      for (auto const &[obj, startPos] : m_fields->movingObjects) {
        current.insert(obj);
      }

      for (auto it = m_fields->persists.begin();
           it != m_fields->persists.end();) {
        if (current.find(it->first) == current.end()) {
          it->first->release();
          it = m_fields->persists.erase(it);
        } else {
          ++it;
        }
      }

      for (auto const &[obj, startPos] : m_fields->movingObjects) {
        CCPoint disp = obj->getPosition() - startPos;
        CCPoint corr = disp;
        bool cx = false;
        bool cy = false;

        if (!cx && std::abs(dp1_prev.x) > 0.0001f) {
          float ratio = disp.x / dp1_prev.x;
          if (isValidFollowRatio(ratio)) {
            corr.x = ratio * dp1_curr.x;
            cx = true;
          }
        }
        if (!cx && std::abs(dp2_prev.x) > 0.0001f) {
          float ratio = disp.x / dp2_prev.x;
          if (isValidFollowRatio(ratio)) {
            corr.x = ratio * dp2_curr.x;
            cx = true;
          }
        }
        if (!cx && std::abs(dcam_prev.x) > 0.0001f) {
          float ratio = disp.x / dcam_prev.x;
          if (isValidFollowRatio(ratio)) {
            corr.x = ratio * dcam_curr.x;
            cx = true;
          }
        }

        if (!cy && std::abs(dp1_prev.y) > 0.0001f) {
          float ratio = disp.y / dp1_prev.y;
          if (isValidFollowRatio(ratio)) {
            corr.y = ratio * dp1_curr.y;
            cy = true;
          }
        }
        if (!cy && std::abs(dp2_prev.y) > 0.0001f) {
          float ratio = disp.y / dp2_prev.y;
          if (isValidFollowRatio(ratio)) {
            corr.y = ratio * dp2_curr.y;
            cy = true;
          }
        }
        if (!cy && std::abs(dcam_prev.y) > 0.0001f) {
          float ratio = disp.y / dcam_prev.y;
          if (isValidFollowRatio(ratio)) {
            corr.y = ratio * dcam_curr.y;
            cy = true;
          }
        }

        auto it = m_fields->persists.find(obj);
        if (it == m_fields->persists.end()) {
          obj->retain();
          m_fields->persists[obj] = {disp, corr, cx, cy};
        } else {
          it->second = {disp, corr, cx, cy};
        }
      }
    }

    std::vector<std::pair<GameObject *, CCPoint>> origObjectPositions;
    if (!dead && hasP1 && m_fields->p1.lastTime != 0) {
      for (auto const &[obj, move] : m_fields->persists) {
        float extX =
            move.corrX ? (move.corrPos.x * (static_cast<float>(camPct) + 1.0f))
                       : (move.lastPos.x * static_cast<float>(camPct));
        float extY =
            move.corrY ? (move.corrPos.y * (static_cast<float>(camPct) + 1.0f))
                       : (move.lastPos.y * static_cast<float>(camPct));

        origObjectPositions.push_back({obj, obj->getPosition()});
        obj->setPosition(obj->getPosition() + CCPoint{extX, extY});
      }
    }

    GJBaseGameLayer::visit();

    if (hasP1 && m_fields->p1.lastTime != 0) {
      m_player1->CCNode::setPosition(origP1);
      m_player1->setRotation(origR1);
    }
    if (hasP2 && m_fields->p2.lastTime != 0) {
      m_player2->CCNode::setPosition(origP2);
      m_player2->setRotation(origR2);
    }
    if (hasObj && camOff != CCPoint{0, 0}) {
      m_objectLayer->setPosition(origObj);
    }
    for (const auto &[node, x] : origGroundX) {
      node->setPositionX(x);
    }
    for (const auto &[obj, pos] : origObjectPositions) {
      obj->setPosition(pos);
    }
    m_fields->m_inVisit = false;
  }
};

class $modify(MyPlayer, PlayerObject) {
  void update(float dt) override {
    auto gameLayer = GJBaseGameLayer::get();
    MyBGL *myGL = nullptr;
    PlayerState *state = nullptr;

    if (gameLayer) {
      myGL = static_cast<MyBGL *>(gameLayer);
      bool isP1 = (this == gameLayer->m_player1);
      state = &(isP1 ? myGL->m_fields->p1 : myGL->m_fields->p2);
    }

    CCPoint posBefore = this->getPosition();
    float rotBefore = this->getRotation();
    CCPoint velBefore = CCPoint(static_cast<float>(this->getCurrentXVelocity()),
                                static_cast<float>(this->m_yVelocity));

    if (state) {
      if (state->steps == 0) {
        state->physInputs = std::move(state->inputs);
        state->inputs.clear();

        state->prevTime = state->lastTime;
        state->prevPos = state->lastPos;
        state->lastPos = posBefore;
        state->prevVel = velBefore;
        state->lastRot = rotBefore;
        state->lastDt = 0;
      } else {
        for (const auto &input : state->inputs) {
          state->physInputs.push_back(input);
        }
        state->inputs.clear();
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

class $modify(MyGameObject, GameObject) {
  void registerMovement() {
    auto gameLayer = GJBaseGameLayer::get();
    if (!gameLayer)
      return;
    if (this == static_cast<GameObject *>(gameLayer->m_player1) ||
        this == static_cast<GameObject *>(gameLayer->m_player2))
      return;

    bool isGround = false;
    CCNode *p = this->getParent();
    while (p) {
      if (geode::cast::typeinfo_cast<GJGroundLayer *>(p)) {
        isGround = true;
        break;
      }
      p = p->getParent();
    }
    if (isGround)
      return;

    auto *myBGL = static_cast<MyBGL *>(gameLayer);
    if (!myBGL->m_fields->m_inVisit) {
      if (myBGL->m_fields->movingObjectsSet.find(this) ==
          myBGL->m_fields->movingObjectsSet.end()) {
        myBGL->m_fields->movingObjectsSet.insert(this);
        myBGL->m_fields->movingObjects.push_back({this, this->getPosition()});
        this->retain();
      }
    }
  }

  void setPosition(CCPoint const &pos) override {
    registerMovement();
    GameObject::setPosition(pos);
  }
};

class $modify(MyPlayLayer, PlayLayer) {
  void resetLevel() override {
    PlayLayer::resetLevel();
    auto myGL = static_cast<MyBGL *>(static_cast<GJBaseGameLayer *>(this));
    for (auto const &[obj, m] : myGL->m_fields->persists) {
      obj->release();
    }
    myGL->m_fields->persists.clear();
  }
};