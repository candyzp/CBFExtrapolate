#include "timestamp.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <vector>

using namespace geode::prelude;

static bool g_hasCBF = false;

$on_mod(Loaded) {
  if (auto cbfMod = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
    g_hasCBF = !cbfMod->getSettingValue<bool>("soft-toggle");
    listenForSettingChanges<bool>(
        "soft-toggle", [](bool value) { g_hasCBF = !value; }, cbfMod);
  }
}

struct Click {
  int button;
  bool push;
  double time;
};

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
  };

  static void onModify(auto &self) {
    (void)self.setHookPriority("GJBaseGameLayer::update", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::visit", Priority::VeryLate);
  }

  void update(float dt) override {
    for (const auto &cmd : m_queuedButtons) {
      auto &state = cmd.m_isPlayer2 ? m_fields->p2 : m_fields->p1;
      state.inputs.push_back({.button = static_cast<int>(cmd.m_button),
                              .push = cmd.m_isPush,
                              .time = cmd.m_timestamp});
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
          state.prog = 0;
          state.lastSteps = state.steps;
          ran = true;
        } else {
          if (dt != 0) {
            state.prog += static_cast<double>(dt) * 60;
          }
          state.lastSteps = 0;
        }
      }
    }

    if (ran && m_objectLayer) {
      m_fields->lastCam = m_objectLayer->getPosition();
      m_fields->prevCam = camBefore;
    }
  }

  void visit() override {
    if (isFlipping()) {
      GJBaseGameLayer::visit();
      return;
    }

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

    bool hasCBF = g_hasCBF;
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
      m_objectLayer->setPosition(origObj + camOff);
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