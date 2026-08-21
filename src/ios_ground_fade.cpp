#include <Geode/Geode.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_IOS
$on_mod(Loaded) {
  for (auto *hook : Mod::get()->getHooks()) {
    auto name = hook->getDisplayName();
    if (name == "GJGroundLayer::fadeInGround" ||
        name == "GJGroundLayer::fadeOutGround") {
      (void)hook->disable();
    }
  }
}
#endif
