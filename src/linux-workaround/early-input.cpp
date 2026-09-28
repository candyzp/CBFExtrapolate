#include "early-input.hpp"

#include "linuxeventcodes.hpp"
#include "timestamp.hpp"
#include <atomic>
#include <cstdint>

#ifdef GEODE_IS_WINDOWS
#include <xinput.h>
#endif

using namespace geode::prelude;

bool g_cbfSoftToggle = false;

static bool g_wineCbfWorkaround = true;

// current version of supported CBF : v1.5.0
static bool cbfVersionSupported() {
#ifdef GEODE_IS_WINDOWS
  auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames");
  return cbf && cbf->getVersion() == geode::VersionInfo(1, 5, 0);
#else
  return false;
#endif
}

static void updateCbfSettingAvailability() {
  Mod::get()->setSavedValue<bool>("cbf-1-5-0", cbfVersionSupported());
}

void earlyInputSetup() {
  g_wineCbfWorkaround =
      Mod::get()->getSettingValue<bool>("wine-cbf-workaround");
  listenForSettingChanges<bool>(
      "wine-cbf-workaround", [](bool value) { g_wineCbfWorkaround = value; });

  if (auto m = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
    g_cbfSoftToggle = m->getSettingValue<bool>("soft-toggle");
    listenForSettingChanges<bool>(
        "soft-toggle", [](bool value) { g_cbfSoftToggle = value; }, m);
  }

  updateCbfSettingAvailability();
  ModStateEvent()
      .listen([](ModEventType, Mod *) { updateCbfSettingAvailability(); })
      .leak();
}

#ifdef GEODE_IS_WINDOWS

// The ring buffer layout and the event mapping are ported from CBF
// (https://github.com/theyareonit/Click-Between-Frames), MIT, (c) 2025
// theyareonit.

constexpr size_t RING_BUFFER_SIZE = 256;

enum DeviceType : int8_t {
  MOUSE,
  TOUCHPAD,
  KEYBOARD,
  TOUCHSCREEN,
  CONTROLLER,
  UNKNOWN
};

struct __attribute__((packed)) LinuxInputEvent {
  int64_t time;
  uint16_t type;
  uint16_t code;
  int32_t value;
  DeviceType deviceType;
};
static_assert(sizeof(LinuxInputEvent) == 17);

struct __attribute__((packed)) SharedMemory {
  volatile uint32_t head;
  volatile uint32_t tail;
  volatile uint32_t error_flag;
  volatile uint32_t heartbeat;
  LinuxInputEvent events[RING_BUFFER_SIZE];
};
static_assert(sizeof(SharedMemory) == 16 + RING_BUFFER_SIZE * 17);

struct CbfInputRing {
  HANDLE file = nullptr;
  HANDLE mapping = nullptr;
  SharedMemory *shm = nullptr;
  uint32_t cursor = 0; // our own read cursor, CBF's tail must never move
  double localAtCalibration = 0.0;
  int64_t ticksAtCalibration = 0;
  double lastOpenAttempt = -1e9;

  void calibrate() {
    localAtCalibration = getCurrentTimestamp();
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ticksAtCalibration =
        (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  }

  void close() {
    if (shm) {
      UnmapViewOfFile(shm);
      shm = nullptr;
    }
    if (mapping) {
      CloseHandle(mapping);
      mapping = nullptr;
    }
    if (file) {
      CloseHandle(file);
      file = nullptr;
    }
  }

  bool open() {
    if (shm) {
      if (getCurrentTimestamp() - localAtCalibration > 30.0) {
        calibrate();
      }
      return true;
    }
    double now = getCurrentTimestamp();
    if (now - lastOpenAttempt < 2.0) {
      return false;
    }
    lastOpenAttempt = now;

    std::string path =
        "Z:\\dev\\shm\\cbf-" + std::to_string(GetCurrentProcessId());
    file = CreateFileA(path.c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      file = nullptr;
      return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart != sizeof(SharedMemory)) {
      close();
      return false;
    }
    mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0,
                                 sizeof(SharedMemory), nullptr);
    if (!mapping) {
      close();
      return false;
    }
    shm = static_cast<SharedMemory *>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SharedMemory)));
    if (!shm) {
      close();
      return false;
    }
    cursor = shm->head; // skip the pre-level backlog
    calibrate();
    log::info("[extrapolate] CBF input ring found");
    return true;
  }

  // subtracting the calibration point as an integer makes it more precise
  double toLocal(int64_t ticks) const {
    return localAtCalibration +
           static_cast<double>(ticks - ticksAtCalibration) * 1e-7;
  }
};
static CbfInputRing g_cbfRing;

enum GameAction : int {
  p1Jump = 0,
  p1Left = 1,
  p1Right = 2,
  p2Jump = 3,
  p2Left = 4,
  p2Right = 5
};

enum State : bool { Release = 0, Press = 1 };

static std::array<std::unordered_set<size_t>, 6> inputBinds;
static std::unordered_set<uint16_t> heldInputs;
static bool enableRightClick = false;

// only activate when cbf wine workaround is active
static bool cbfRingActive() {
  if (!g_wineCbfWorkaround || g_cbfSoftToggle || !cbfVersionSupported()) {
    return false;
  }
  return g_cbfRing.open();
}

// ported from CBF's updateKeybinds()
void refreshCbfInputBinds() {
  std::array<std::unordered_set<size_t>, 6> binds;
  std::vector<geode::Keybind> v;
  Mod *customKeybinds = Loader::get()->getLoadedMod("geode.custom-keybinds");
  if (!customKeybinds) {
    inputBinds[p1Jump] = {cocos2d::KEY_Space, cocos2d::KEY_W,
                          cocos2d::CONTROLLER_A, cocos2d::CONTROLLER_Up,
                          cocos2d::CONTROLLER_RB};
    inputBinds[p1Left] = {cocos2d::KEY_A, cocos2d::CONTROLLER_Left,
                          cocos2d::CONTROLLER_LTHUMBSTICK_LEFT};
    inputBinds[p1Right] = {cocos2d::KEY_D, cocos2d::CONTROLLER_Right,
                           cocos2d::CONTROLLER_LTHUMBSTICK_RIGHT};

    inputBinds[p2Jump] = {cocos2d::KEY_Up, cocos2d::CONTROLLER_LB};
    inputBinds[p2Left] = {cocos2d::KEY_Left,
                          cocos2d::CONTROLLER_RTHUMBSTICK_LEFT};
    inputBinds[p2Right] = {cocos2d::KEY_Right,
                           cocos2d::CONTROLLER_RTHUMBSTICK_RIGHT};
    return;
  }

  auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames");
  if (!cbf) {
    return;
  }
  enableRightClick = cbf->getSettingValue<bool>("right-click");

  v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("jump-p1");
  for (size_t i = 0; i < v.size(); i++)
    binds[p1Jump].emplace(v[i].key);

  v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>(
      "move-left-p1");
  for (size_t i = 0; i < v.size(); i++)
    binds[p1Left].emplace(v[i].key);

  v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>(
      "move-right-p1");
  for (size_t i = 0; i < v.size(); i++)
    binds[p1Right].emplace(v[i].key);

  v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>("jump-p2");
  for (size_t i = 0; i < v.size(); i++)
    binds[p2Jump].emplace(v[i].key);

  v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>(
      "move-left-p2");
  for (size_t i = 0; i < v.size(); i++)
    binds[p2Left].emplace(v[i].key);

  v = customKeybinds->getSettingValue<std::vector<geode::Keybind>>(
      "move-right-p2");
  for (size_t i = 0; i < v.size(); i++)
    binds[p2Right].emplace(v[i].key);

  inputBinds = binds;
}

static bool isCbfClickTarget(bool player2, bool player2Block,
                             bool isTwoPlayer) {
  return player2Block ? (player2 || !isTwoPlayer) : (!player2 || !isTwoPlayer);
}

static void addEarlyClick(std::vector<PlayerButtonCommand> &out,
                          double &targetOffset, double lastTime,
                          double sampleSeconds, double stepSeconds,
                          PlayerButton button, bool player2, bool isPush,
                          double time) {
  if (time <= lastTime || time > lastTime + stepSeconds) {
    return; // already applied or past the one-engine-step policy
  }
  PlayerButtonCommand cmd;
  cmd.m_button = button;
  cmd.m_isPush = isPush;
  cmd.m_isPlayer2 = player2;
  cmd.m_timestamp = time;
  out.push_back(cmd);

  double clickOffset = time - lastTime;
  if (clickOffset > sampleSeconds) {
    targetOffset = std::max(targetOffset,
                            std::min(clickOffset + sampleSeconds, stepSeconds));
  }
}

// ported from CBF's linuxCheckInputs(), main difference is that
// we don't move the tail
static double collectRingClicks(std::vector<PlayerButtonCommand> &out,
                                double lastTime, double sampleSeconds,
                                double stepSeconds, bool player2Block,
                                bool isTwoPlayer) {
  double targetOffset = sampleSeconds;
  SharedMemory *shm = g_cbfRing.shm;
  if (!shm) {
    return lastTime + targetOffset;
  }

  static std::unordered_map<int, cocos2d::enumKeyCodes> linuxToCCKey = {
      {BTN_A, cocos2d::CONTROLLER_A},
      {BTN_B, cocos2d::CONTROLLER_B},
      {BTN_X, cocos2d::CONTROLLER_X},
      {BTN_Y, cocos2d::CONTROLLER_Y},
      {BTN_TL, cocos2d::CONTROLLER_LB},
      {BTN_TR, cocos2d::CONTROLLER_RB},
      {BTN_SELECT, cocos2d::CONTROLLER_Back},
      {BTN_START, cocos2d::CONTROLLER_Start},
  };

  uint32_t h = shm->head;
  std::atomic_thread_fence(std::memory_order_acquire);
  uint32_t t = g_cbfRing.cursor;
  // we fell behind and the helper overwrote events
  if (h - t > RING_BUFFER_SIZE) {
    t = h;
    heldInputs.clear();
  }

  while (t != h) {
    const LinuxInputEvent &ev = shm->events[t & (RING_BUFFER_SIZE - 1)];
    t++;

    PlayerButtonCommand input;
    bool player1 = true;
    USHORT scanCode = ev.code;
    int value = ev.value;

    switch (ev.deviceType) {
    case MOUSE:
    case TOUCHPAD:
      if (scanCode == BUTTON_LEFT) {
        input.m_button = PlayerButton::Jump;
      } else if (scanCode == BUTTON_RIGHT) {
        if (!enableRightClick)
          continue;
        input.m_button = PlayerButton::Jump;
        player1 = false;
      } else {
        continue; // m_button would be uninitialized
      }
      break;
    case KEYBOARD: {
      USHORT keyCode =
          MapVirtualKeyExA(scanCode, MAPVK_VSC_TO_VK, GetKeyboardLayout(0));
      if (inputBinds[p1Jump].contains(keyCode))
        input.m_button = PlayerButton::Jump;
      else if (inputBinds[p1Left].contains(keyCode))
        input.m_button = PlayerButton::Left;
      else if (inputBinds[p1Right].contains(keyCode))
        input.m_button = PlayerButton::Right;
      else {
        player1 = false;
        if (inputBinds[p2Jump].contains(keyCode))
          input.m_button = PlayerButton::Jump;
        else if (inputBinds[p2Left].contains(keyCode))
          input.m_button = PlayerButton::Left;
        else if (inputBinds[p2Right].contains(keyCode))
          input.m_button = PlayerButton::Right;
        else
          continue;
      }
      break;
    }
    case TOUCHSCREEN:
      if (scanCode == BTN_TOUCH) {
        input.m_button = PlayerButton::Jump;
      } else {
        continue; // m_button would be uninitialized
      }
      break;
    case CONTROLLER: {
      int keyCode = -1;
      if (ev.type == EV_KEY) {
        keyCode = linuxToCCKey[scanCode];
      } else if (ev.type == EV_ABS) {
        bool continueLoop = false;
        auto analyze4Directions = [&](int deadzone,
                                      cocos2d::enumKeyCodes negative,
                                      cocos2d::enumKeyCodes positive) {
          if (ev.value < -deadzone) {
            keyCode = negative;
            if (heldInputs.contains(negative)) {
              continueLoop = true;
            }
            value = Press;
          } else if (ev.value > deadzone) {
            keyCode = positive;
            if (heldInputs.contains(positive)) {
              continueLoop = true;
            }
            value = Press;
          } else {
            value = Release;
            if (heldInputs.contains(negative)) {
              keyCode = negative;
            } else if (heldInputs.contains(positive)) {
              keyCode = positive;
            } else {
              continueLoop = true;
            }
          }
        };

        switch (ev.code) {
        case ABS_X:
          analyze4Directions(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
                             cocos2d::CONTROLLER_LTHUMBSTICK_LEFT,
                             cocos2d::CONTROLLER_LTHUMBSTICK_RIGHT);
          break;
        case ABS_Y:
          analyze4Directions(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
                             cocos2d::CONTROLLER_LTHUMBSTICK_UP,
                             cocos2d::CONTROLLER_LTHUMBSTICK_DOWN);
          break;
        case ABS_RX:
          analyze4Directions(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,
                             cocos2d::CONTROLLER_RTHUMBSTICK_LEFT,
                             cocos2d::CONTROLLER_RTHUMBSTICK_RIGHT);
          break;
        case ABS_RY:
          analyze4Directions(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,
                             cocos2d::CONTROLLER_RTHUMBSTICK_UP,
                             cocos2d::CONTROLLER_RTHUMBSTICK_DOWN);
          break;
        case ABS_HAT0X:
          analyze4Directions(10, cocos2d::CONTROLLER_Left,
                             cocos2d::CONTROLLER_Right);
          break;
        case ABS_HAT0Y:
          analyze4Directions(10, cocos2d::CONTROLLER_Up,
                             cocos2d::CONTROLLER_Down);
          break;
        case ABS_Z:
          keyCode = cocos2d::CONTROLLER_LT;
          if (ev.value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
            value = Press;
          } else {
            value = Release;
          }
          break;
        case ABS_RZ:
          keyCode = cocos2d::CONTROLLER_RT;
          if (ev.value > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
            value = Press;
          } else {
            value = Release;
          }
          break;
        }
        if (continueLoop)
          continue;
      }
      if (inputBinds[p1Jump].contains(keyCode))
        input.m_button = PlayerButton::Jump;
      else if (inputBinds[p1Left].contains(keyCode))
        input.m_button = PlayerButton::Left;
      else if (inputBinds[p1Right].contains(keyCode))
        input.m_button = PlayerButton::Right;
      else {
        player1 = false;
        if (inputBinds[p2Jump].contains(keyCode))
          input.m_button = PlayerButton::Jump;
        else if (inputBinds[p2Left].contains(keyCode))
          input.m_button = PlayerButton::Left;
        else if (inputBinds[p2Right].contains(keyCode))
          input.m_button = PlayerButton::Right;
        else
          continue;
      }
      if (value == Press) {
        if (heldInputs.contains(keyCode)) {
          continue;
        } else {
          heldInputs.emplace(keyCode);
        }
      } else {
        if (!heldInputs.contains(keyCode)) {
          continue;
        } else {
          heldInputs.erase(keyCode);
        }
      }
      break;
    }
    default:
      continue;
    }

    input.m_isPush = value;
    input.m_timestamp = g_cbfRing.toLocal(ev.time);
    input.m_isPlayer2 = !player1;

    if (isCbfClickTarget(input.m_isPlayer2, player2Block, isTwoPlayer)) {
      addEarlyClick(out, targetOffset, lastTime, sampleSeconds, stepSeconds,
                    input.m_button, input.m_isPlayer2, input.m_isPush,
                    input.m_timestamp);
    }
  }

  g_cbfRing.cursor = t;
  return lastTime + targetOffset;
}

bool collectEarlyClicks(std::vector<PlayerButtonCommand> &out, double &target,
                        double lastTime, double sampleSeconds,
                        double stepSeconds, bool player2Block,
                        bool isTwoPlayer) {
  if (cbfRingActive()) {
    target = collectRingClicks(out, lastTime, sampleSeconds, stepSeconds,
                               player2Block, isTwoPlayer);
    return true;
  }
  return false;
}
#else
bool collectEarlyClicks(std::vector<PlayerButtonCommand> &, double &, double,
                        double, double, bool, bool) {
  return false;
}
#endif
