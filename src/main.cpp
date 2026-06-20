#include "timestamp.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/RingObject.hpp>
#include <Geode/binding/DashRingObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/RingObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
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

static std::atomic<bool> g_hasCBF = false;
static bool g_softToggle = false;
static bool g_extrapolating = false;

#ifdef GEODE_IS_WINDOWS
enum GameAction : int {
  p1Jump = 0,
  p1Left = 1,
  p1Right = 2,
  p2Jump = 3,
  p2Left = 4,
  p2Right = 5
};

static std::array<std::unordered_set<size_t>, 6> g_inputBinds;
static std::mutex g_keybindsMutex;

static void updateKeybinds() {
  std::array<std::unordered_set<size_t>, 6> binds;
  if (auto keybindsMod = Loader::get()->getLoadedMod("geode.custom-keybinds")) {
    auto populateBind = [&](const char *settingKey, GameAction action) {
      auto vec = keybindsMod->getSettingValue<std::vector<Keybind>>(settingKey);
      for (const auto &bind : vec) {
        binds[action].emplace(static_cast<size_t>(bind.key));
      }
    };
    populateBind("jump-p1", p1Jump);
    populateBind("move-left-p1", p1Left);
    populateBind("move-right-p1", p1Right);
    populateBind("jump-p2", p2Jump);
    populateBind("move-left-p2", p2Left);
    populateBind("move-right-p2", p2Right);
  }
  {
    std::lock_guard<std::mutex> lock(g_keybindsMutex);
    g_inputBinds = binds;
  }
}
#endif

static void extrapolatePushButton(PlayerObject *player, PlayerButton button) {
  player->m_holdingButtons[static_cast<int>(button)] = true;
  if (button == PlayerButton::Jump) {
    player->m_jumpBuffered = true;
    player->m_hasEverJumped = true;
  } else if (button == PlayerButton::Left) {
    player->m_holdingLeft = true;
  } else if (button == PlayerButton::Right) {
    player->m_holdingRight = true;
  }
}

static void extrapolateReleaseButton(PlayerObject *player,
                                     PlayerButton button) {
  player->m_holdingButtons[static_cast<int>(button)] = false;
  if (button == PlayerButton::Jump) {
    player->m_jumpBuffered = false;
  } else if (button == PlayerButton::Left) {
    player->m_holdingLeft = false;
  } else if (button == PlayerButton::Right) {
    player->m_holdingRight = false;
  }
}

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#include <winuser.h>

struct RawInputEvent {
  double timestamp;
  PlayerButton button;
  bool isPush;
  bool isPlayer2;
};

static std::vector<RawInputEvent> g_rawInputs;
static std::mutex g_rawInputsMutex;
static WNDPROC g_originalWndProc = nullptr;

static LRESULT CALLBACK ExtrapolateWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                           LPARAM lParam) {
  if (g_hasCBF.load() && !g_softToggle) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP ||
        msg == WM_SYSKEYUP) {
      USHORT vkey = static_cast<USHORT>(wParam);
      bool isPush = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
      bool isRepeat = (lParam & (1 << 30)) != 0;

      if (!(isPush && isRepeat)) {
        PlayerButton button = PlayerButton::Jump;
        bool isPlayer2 = false;
        bool valid = false;

        if (vkey >= VK_NUMPAD0 && vkey <= VK_NUMPAD9) {
          vkey -= 0x30;
        }

        {
          std::lock_guard<std::mutex> lock(g_keybindsMutex);
          if (g_inputBinds[p1Jump].contains(vkey)) {
            button = PlayerButton::Jump;
            isPlayer2 = false;
            valid = true;
          } else if (g_inputBinds[p1Left].contains(vkey)) {
            button = PlayerButton::Left;
            isPlayer2 = false;
            valid = true;
          } else if (g_inputBinds[p1Right].contains(vkey)) {
            button = PlayerButton::Right;
            isPlayer2 = false;
            valid = true;
          } else if (g_inputBinds[p2Jump].contains(vkey)) {
            button = PlayerButton::Jump;
            isPlayer2 = true;
            valid = true;
          } else if (g_inputBinds[p2Left].contains(vkey)) {
            button = PlayerButton::Left;
            isPlayer2 = true;
            valid = true;
          } else if (g_inputBinds[p2Right].contains(vkey)) {
            button = PlayerButton::Right;
            isPlayer2 = true;
            valid = true;
          }
        }

        if (valid) {
          bool flip2Player =
              GameManager::sharedState()->getGameVariable("0010");
          if (flip2Player) {
            isPlayer2 = !isPlayer2;
          }

          DWORD msgTime = GetMessageTime();
          DWORD currentTime = GetTickCount();
          DWORD diffMs = currentTime - msgTime;
          double diffSec = static_cast<double>(diffMs) / 1000.0;
          double timestamp = getCurrentTimestamp() - diffSec;

          std::lock_guard<std::mutex> lock(g_rawInputsMutex);
          bool duplicate = false;
          if (!g_rawInputs.empty()) {
            const auto &last = g_rawInputs.back();
            if (last.button == button && last.isPush == isPush && last.isPlayer2 == isPlayer2 &&
                std::abs(last.timestamp - timestamp) < 0.005) {
              duplicate = true;
            }
          }
          if (!duplicate) {
            g_rawInputs.push_back({timestamp, button, isPush, isPlayer2});
          }
        }
      }
    } else if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
               msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
      bool isPush = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN);
      bool isPlayer2 = (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP);
      PlayerButton button = PlayerButton::Jump;

      bool flip2Player = GameManager::sharedState()->getGameVariable("0010");
      if (flip2Player) {
        isPlayer2 = !isPlayer2;
      }

      DWORD msgTime = GetMessageTime();
      DWORD currentTime = GetTickCount();
      DWORD diffMs = currentTime - msgTime;
      double diffSec = static_cast<double>(diffMs) / 1000.0;
      double timestamp = getCurrentTimestamp() - diffSec;

      std::lock_guard<std::mutex> lock(g_rawInputsMutex);
      bool duplicate = false;
      if (!g_rawInputs.empty()) {
        const auto &last = g_rawInputs.back();
        if (last.button == button && last.isPush == isPush && last.isPlayer2 == isPlayer2 &&
            std::abs(last.timestamp - timestamp) < 0.005) {
          duplicate = true;
        }
      }
      if (!duplicate) {
        g_rawInputs.push_back({timestamp, button, isPush, isPlayer2});
      }
    }
  }

  if (g_originalWndProc) {
    return CallWindowProcA(g_originalWndProc, hwnd, msg, wParam, lParam);
  }
  return DefWindowProcA(hwnd, msg, wParam, lParam);
}
#endif

$on_mod(Loaded) {
  g_softToggle = Mod::get()->getSettingValue<bool>("soft-toggle");
  listenForSettingChanges<bool>("soft-toggle",
                                [](bool value) { g_softToggle = value; });

  if (auto cbfMod = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
    g_hasCBF = !cbfMod->getSettingValue<bool>("soft-toggle");
    listenForSettingChanges<bool>(
        "soft-toggle", [](bool value) { g_hasCBF = !value; }, cbfMod);
  } else {
    g_hasCBF = false;
  }

  if (auto keybindsMod = Loader::get()->getLoadedMod("geode.custom-keybinds")) {
    updateKeybinds();

    auto listenKeybind = [keybindsMod](const char *settingKey) {
      listenForSettingChanges<std::vector<Keybind>>(
          settingKey, [](std::vector<Keybind> const &) { updateKeybinds(); },
          keybindsMod);
    };

    listenKeybind("jump-p1");
    listenKeybind("move-left-p1");
    listenKeybind("move-right-p1");
    listenKeybind("jump-p2");
    listenKeybind("move-left-p2");
    listenKeybind("move-right-p2");
  }
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

  fake->m_holdingLeft = real->m_holdingLeft;
  fake->m_holdingRight = real->m_holdingRight;
  fake->m_holdingButtons = real->m_holdingButtons;
  fake->m_jumpBuffered = real->m_jumpBuffered;
  fake->m_wasJumpBuffered = real->m_wasJumpBuffered;
  fake->m_hasEverJumped = real->m_hasEverJumped;
  fake->m_isDashing = real->m_isDashing;
  fake->m_isDead = real->m_isDead;
  fake->m_inputsLocked = real->m_inputsLocked;

  fake->m_isShip = real->m_isShip;
  fake->m_isBird = real->m_isBird;
  fake->m_isBall = real->m_isBall;
  fake->m_isDart = real->m_isDart;
  fake->m_isRobot = real->m_isRobot;
  fake->m_isSpider = real->m_isSpider;
  fake->m_isSwing = real->m_isSwing;
  fake->m_playEffects = false;
}

static bool isFakePlayer(PlayerObject* player);

class $modify(MyBGL, GJBaseGameLayer) {
  struct Fields {
    PlayerState p1;
    PlayerState p2;
    CCPoint m_lastCamDisp = {0, 0};
    CCPoint m_camDispAccum = {0, 0};
    bool m_hasNewCamDisp = false;
    std::vector<std::pair<CCNode *, float>> origGroundX;
    PlayerObject *m_fakePlayer1 = nullptr;
    PlayerObject *m_fakePlayer2 = nullptr;

    ~Fields() {
      if (m_fakePlayer1) {
        m_fakePlayer1->release();
      }
      if (m_fakePlayer2) {
        m_fakePlayer2->release();
      }
    }
  };

  static void onModify(auto &self) {
    (void)self.setHookPriority("GJBaseGameLayer::update", Priority::VeryEarly);
    (void)self.setHookPriority("GJBaseGameLayer::updateCamera", Priority::VeryLate);
    (void)self.setHookPriority("GJBaseGameLayer::visit", Priority::VeryLate);
    (void)self.setHookPriority("GJBaseGameLayer::flipGravity",
                               Priority::VeryEarly);
  }

  void flipGravity(PlayerObject *player, bool flip, bool noEffects) {
    if (isFakePlayer(player)) {
      GJBaseGameLayer::flipGravity(player, flip, true);
      return;
    }
    GJBaseGameLayer::flipGravity(player, flip, noEffects);
  }

  void updateCamera(float dt) {
    if (g_softToggle) {
      GJBaseGameLayer::updateCamera(dt);
      return;
    }

    if (m_fields->p1.steps == 0 && m_fields->p2.steps == 0) {
      GJBaseGameLayer::updateCamera(0.f);
      return;
    }

    CCPoint camBefore = m_objectLayer ? m_objectLayer->getPosition() : CCPoint{0, 0};
    GJBaseGameLayer::updateCamera(dt);
    if (m_objectLayer) {
      CCPoint disp = m_objectLayer->getPosition() - camBefore;
      m_fields->m_camDispAccum += disp;
      m_fields->m_hasNewCamDisp = true;
    }
  }

  void update(float dt) override {
    if (g_softToggle) {
      GJBaseGameLayer::update(dt);
      return;
    }

    m_fields->m_camDispAccum = CCPoint(0.f, 0.f);
    m_fields->m_hasNewCamDisp = false;
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
    auto player = PlayerObject::create(1, 1, this, m_objectLayer, true);
    if (player) {
      player->retain();
      player->setVisible(false);
      player->m_isSecondPlayer = isPlayer2;
      player->m_playEffects = false;
      m_objectLayer->addChild(player);
    }
    return player;
  }

  void visit() override {
    bool paused = false;
    auto playLayer = geode::cast::typeinfo_cast<PlayLayer *>(this);
    if (playLayer) {
      paused = playLayer->getChildByType<PauseLayer>(0) != nullptr ||
               CCDirector::sharedDirector()
                       ->getRunningScene()
                       ->getChildByType<PauseLayer>(0) != nullptr;
    }

    if (g_softToggle || isFlipping() || paused) {
      GJBaseGameLayer::visit();
      return;
    }

    if (m_fields->m_hasNewCamDisp) {
      m_fields->m_lastCamDisp = m_fields->m_camDispAccum;
      m_fields->m_hasNewCamDisp = false;
    }

    bool origPlayerDied = m_playerDied;
    bool hasP1 = m_player1 != nullptr;
    bool hasP2 = m_player2 != nullptr;

    if (m_objectLayer) {
      if (hasP1) {
        if (!m_fields->m_fakePlayer1 ||
            m_fields->m_fakePlayer1->getParent() != m_objectLayer) {
          if (m_fields->m_fakePlayer1) {
            m_fields->m_fakePlayer1->release();
            m_fields->m_fakePlayer1 = nullptr;
          }
          m_fields->m_fakePlayer1 = createFakePlayer(false);
        }
      }
      if (hasP2) {
        if (!m_fields->m_fakePlayer2 ||
            m_fields->m_fakePlayer2->getParent() != m_objectLayer) {
          if (m_fields->m_fakePlayer2) {
            m_fields->m_fakePlayer2->release();
            m_fields->m_fakePlayer2 = nullptr;
          }
          m_fields->m_fakePlayer2 = createFakePlayer(true);
        }
      }
    }

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

          auto updatePlayerSubstepped = [&](double dtFrames) {
            double remaining = dtFrames;
            double stepSize = 0.25;
            while (remaining > 0.0) {
              double currentStep = std::min(remaining, stepSize);
              player->update(static_cast<float>(currentStep));

              float collisionDt = static_cast<float>(currentStep);
              if (!player->m_isOnSlope || player->m_isDart) {
                collisionDt = 0.0f;
              }
              this->checkCollisions(player, collisionDt, true);

              player->m_isDead = false;

              remaining -= currentStep;
            }
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
        double tCurrent = getCurrentTimestamp();
        double timeScale = m_gameState.m_timeWarp;
        double dtSeconds = tCurrent - state.lastTime;
        double maxDtSeconds = (state.lastDt > 0.0001f) ? ((state.lastDt / 60.0f) / timeScale) : 0.033;
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds > 0.0 && dtSeconds < 2.0) {
          std::vector<PlayerButtonCommand> pendingClicks;
#ifdef GEODE_IS_WINDOWS
          if (g_hasCBF) {
            std::lock_guard<std::mutex> lock(g_rawInputsMutex);
            for (const auto &cmd : g_rawInputs) {
              if (!cmd.isPlayer2 && cmd.timestamp > state.lastTime &&
                  cmd.timestamp <= tCurrentClamped) {
                PlayerButtonCommand pbc;
                pbc.m_button = cmd.button;
                pbc.m_isPush = cmd.isPush;
                pbc.m_isPlayer2 = cmd.isPlayer2;
                pbc.m_timestamp = cmd.timestamp;
                pendingClicks.push_back(pbc);
              }
            }
          } else {
            for (const auto &cmd : m_queuedButtons) {
              if (!cmd.m_isPlayer2 && cmd.m_timestamp > state.lastTime &&
                  cmd.m_timestamp <= tCurrentClamped) {
                pendingClicks.push_back(cmd);
              }
            }
          }
#else
          for (const auto &cmd : m_queuedButtons) {
            if (!cmd.m_isPlayer2 && cmd.m_timestamp > state.lastTime &&
                cmd.m_timestamp <= tCurrentClamped) {
              pendingClicks.push_back(cmd);
            }
          }
#endif

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

          float rotSpeed = 0.0f;
          if (state.lastDt > 0) {
            float rDisp = origR1 - state.lastRot;
            if (rDisp > 180)
              rDisp -= 360;
            if (rDisp < -180)
              rDisp += 360;
            rotSpeed = rDisp / state.lastDt;
          }
          float dtFrames = static_cast<float>(dtSeconds) * 60.0f;
          float rot = origR1 + rotSpeed * dtFrames;

          m_player1->CCNode::setPosition(
              m_fields->m_fakePlayer1->getPosition());
          m_player1->setRotation(rot);

          CCPoint camDisp = m_fields->m_lastCamDisp;
          if (maxDtSeconds > 0.0001) {
            camPct = dtSeconds / maxDtSeconds;
            if (camPct > 1.0) camPct = 1.0;
            if (camPct < 0.0) camPct = 0.0;
            camOff = camDisp * static_cast<float>(camPct);
          }
        }
      }
    }

    if (hasP2 && m_fields->m_fakePlayer2) {
      auto &state = m_fields->p2;
      if (state.lastTime != 0 && !dead) {
        double tCurrent = getCurrentTimestamp();
        double timeScale = m_gameState.m_timeWarp;
        double dtSeconds = tCurrent - state.lastTime;
        double maxDtSeconds = (state.lastDt > 0.0001f) ? ((state.lastDt / 60.0f) / timeScale) : 0.033;
        if (dtSeconds > maxDtSeconds) {
          dtSeconds = maxDtSeconds;
        }
        double tCurrentClamped = state.lastTime + dtSeconds;

        if (dtSeconds > 0.0 && dtSeconds < 2.0) {
          std::vector<PlayerButtonCommand> pendingClicks;
#ifdef GEODE_IS_WINDOWS
          if (g_hasCBF) {
            std::lock_guard<std::mutex> lock(g_rawInputsMutex);
            for (const auto &cmd : g_rawInputs) {
              if (cmd.isPlayer2 && cmd.timestamp > state.lastTime &&
                  cmd.timestamp <= tCurrentClamped) {
                PlayerButtonCommand pbc;
                pbc.m_button = cmd.button;
                pbc.m_isPush = cmd.isPush;
                pbc.m_isPlayer2 = cmd.isPlayer2;
                pbc.m_timestamp = cmd.timestamp;
                pendingClicks.push_back(pbc);
              }
            }
          } else {
            for (const auto &cmd : m_queuedButtons) {
              if (cmd.m_isPlayer2 && cmd.m_timestamp > state.lastTime &&
                  cmd.m_timestamp <= tCurrentClamped) {
                pendingClicks.push_back(cmd);
              }
            }
          }
#else
          for (const auto &cmd : m_queuedButtons) {
            if (cmd.m_isPlayer2 && cmd.m_timestamp > state.lastTime &&
                cmd.m_timestamp <= tCurrentClamped) {
              pendingClicks.push_back(cmd);
            }
          }
#endif

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

          float rotSpeed = 0.0f;
          if (state.lastDt > 0) {
            float rDisp = origR2 - state.lastRot;
            if (rDisp > 180)
              rDisp -= 360;
            if (rDisp < -180)
              rDisp += 360;
            rotSpeed = rDisp / state.lastDt;
          }
          float dtFrames = static_cast<float>(dtSeconds) * 60.0f;
          float rot = origR2 + rotSpeed * dtFrames;

          m_player2->CCNode::setPosition(
              m_fields->m_fakePlayer2->getPosition());
          m_player2->setRotation(rot);
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
      float move = m_fields->m_lastCamDisp.x;
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
  }
};

static bool isFakePlayer(PlayerObject* player) {
  if (!player) return false;
  auto gameLayer = player->m_gameLayer;
  if (!gameLayer) return false;
  auto myGL = static_cast<MyBGL*>(gameLayer);
  return player == myGL->m_fields->m_fakePlayer1 || player == myGL->m_fields->m_fakePlayer2;
}

class $modify(MyPlayer, PlayerObject) {
  static void onModify(auto &self) {
    (void)self.setHookPriority("PlayerObject::update", Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::collidedWithObject",
                               Priority::VeryEarly);
    (void)self.setHookPriority("PlayerObject::playerDestroyed",
                               Priority::VeryEarly);
  }

  void playerDestroyed(bool noEffects) {
    if (isFakePlayer(this)) {
      return;
    }
    PlayerObject::playerDestroyed(noEffects);
  }

  bool collidedWithObject(float dt, GameObject *object, cocos2d::CCRect rect,
                          bool skipCheck) {
    if (isFakePlayer(this)) {
      if (object) {
        auto type = object->getType();
        if (type == GameObjectType::Solid || type == GameObjectType::Slope ||
            type == GameObjectType::Breakable ||
            type == GameObjectType::CollisionObject) {
          return PlayerObject::collidedWithObject(dt, object, rect, skipCheck);
        }
      }
      return false;
    }
    return PlayerObject::collidedWithObject(dt, object, rect, skipCheck);
  }

  void update(float dt) override {
    if (g_softToggle) {
      PlayerObject::update(dt);
      return;
    }

    auto gameLayer = this->m_gameLayer;
    MyBGL *myGL = nullptr;
    if (gameLayer) {
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

#ifdef GEODE_IS_WINDOWS
      {
        std::lock_guard<std::mutex> lock(g_rawInputsMutex);
        g_rawInputs.erase(std::remove_if(g_rawInputs.begin(), g_rawInputs.end(),
                                         [&](const RawInputEvent &ev) {
                                           return ev.timestamp <
                                                  state->lastTime - 2.0;
                                         }),
                          g_rawInputs.end());
      }
#endif
    }
  }
};

class $modify(MyPlayLayer, PlayLayer) {
  static void onModify(auto &self) {
    (void)self.setHookPriority("PlayLayer::init", Priority::VeryEarly);
    (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::VeryEarly);
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

#ifdef GEODE_IS_WINDOWS
    updateKeybinds();
#endif
    return true;
  }

  void resetExtrapolation() {
    auto myGL = static_cast<MyBGL *>(static_cast<GJBaseGameLayer *>(this));
    if (myGL) {
      myGL->m_fields->p1 = PlayerState();
      myGL->m_fields->p2 = PlayerState();
      myGL->m_fields->m_lastCamDisp = CCPoint(0.f, 0.f);
      myGL->m_fields->m_camDispAccum = CCPoint(0.f, 0.f);
      myGL->m_fields->m_hasNewCamDisp = false;
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
    if (g_extrapolating) {
      return;
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

#include <Geode/modify/MenuLayer.hpp>

class $modify(MyMenuLayer, MenuLayer) {
  bool init() {
    if (!MenuLayer::init())
      return false;

#ifdef GEODE_IS_WINDOWS
    static bool subclassed = false;
    if (!subclassed) {
      HWND hwnd = WindowFromDC(wglGetCurrentDC());
      if (hwnd) {
        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(ExtrapolateWndProc)));
        subclassed = true;
      }
    }
#endif

    return true;
  }
};

class $modify(MyEnhancedGameObject, EnhancedGameObject) {
  void activatedByPlayer(PlayerObject* player) {
    if (isFakePlayer(player)) {
      return;
    }
    EnhancedGameObject::activatedByPlayer(player);
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