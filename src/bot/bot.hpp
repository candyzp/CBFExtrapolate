#pragma once

#include <Geode/Geode.hpp>

class Trajectory;

struct ReplaySystem {
  bool m_maintainGravity = false;
};

struct Updater {
  float m_lastPlayerX = 0.0f;
  float m_currentPlayerX = 0.0f;
  bool m_predicting = false;
  struct PreventDeath {
    bool inner() { return false; }
  } *m_preventDeath = nullptr;
  struct CanDie {
    bool inner() { return true; }
  } *m_canDie = nullptr;
  struct LayoutMode {
    bool inner() { return false; }
  } *m_layoutMode = nullptr;
};

class Bot {
public:
  static Bot *get() {
    static Bot instance;
    return &instance;
  }

  Trajectory &trajectory();

  ReplaySystem &replaySystem() {
    static ReplaySystem rs;
    return rs;
  }

  Updater &updater() {
    static Updater u;
    return u;
  }

  bool isRecording() { return false; }
  bool isPlaying() { return false; }
  bool isEnabled() { return false; }
};
