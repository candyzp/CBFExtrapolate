# 2.2.11
- Enabled solid collisions unconditionally on every sub-step to fully resolve the flat-surface falling/jitter bug for all game modes, while implementing a physical velocity-direction filter to reject any vertical push-outs that assist the player's vertical velocity (resolving the corner wall-climbing/teleporting bugs).

# 2.2.10
- Fixed a bug where the screen would shake infinitely upon actual player death by restoring the `!dead` (checking only `m_isDead` flag) checks to the extrapolation triggers.

# 2.2.9
- Fixed a game crash (Access Violation / DEP violation at 0x0) occurring during `MyBGL::visit` when trying to restore positions of ground child nodes that were deleted during standard visit traversal by implementing an active-tree validation check.

# 2.2.8
- Fixed a bug where crashing under Noclip (especially in Wave mode) would permanently disable extrapolation for the rest of the attempt.
- implemented explicit copying of special block states (D-blocks, J-blocks, H-blocks, force blocks) and dash attributes during player state synchronization to natively support Wave sliding and other gameplay elements.

# 2.2.7
- Expanded camera trigger extrapolation to include Static Camera, Camera Zoom, Camera Target, Gameplay Offset, and auxiliary target follow triggers.
- Fully restored all modified camera states in SafeGameState to prevent state pollution.
- Fixed a game freeze bug caused by division-by-near-zero in Player 1's extrapolation timeScale calculation.
- Fixed player interpolation stutter/freeze under Noclip mode when clipping inside solid blocks or passing through walls.
- Bypassed player simulation halts at wall impacts by enabling `ignoreDamage` natively during extrapolation.

# 2.2.6
- Fixed physics-affecting camera tween bugs that caused object misalignments.
- Fixed temporary blur/shader effect glitches occurring on restarting attempts.
- Fixed practice mode and startpos input buffering/hold click bugs by preserving queued inputs across attempts.

# 2.2.5
- Fixed game crashes occurring when resetting levels, players dying, or changing Startpos.

# 2.2.4
- Added extrapolation for camera offset and zoom triggers and etc.

# 2.2.3
- Fixed stuck input (autojump/remains pressed) bugs when resetting/restarting levels.
- Disabled player rotation extrapolation entirely to prevent bugs.

# 2.2.2
- Optimized and cleaned up internal extrapolation code.

# 2.2.1
- Fixed mirror portal issue (probably).
- Fixed player stuttering/jittering under low game speed warp settings (e.g. 0.25x).

# 2.2.0
- Fixed camera trigger overshooting by calling actual game's updateCamera function and restoring state in physics tick.
- Fixed player overshooting when player hits speed portals.

# 2.1.1
- Fixed a crash on level reset by reusing fake player objects instead of recreating them.

# 2.1.0
- Fixed ground layer position offset issue during camera y-axis interpolation.
- Fixed memory leak of fake players causing FPS drops over time.

# 2.0.11
- Added camera zoom extrapolation
- Modified soft toggle to disable all hooks

# 2.0.10
- Disabled visual extrapolation in Platformer mode.

# 2.0.9
- Added cheat tags because it could be considered a cheat on leaderboards or forums.

# 2.0.8
- changed fake player's parent to 'this'
- blocked extrapolation in editor

# 2.0.7
- Fixed the issue where the object activation state was determined by the fake player.

# 2.0.6
- added simulation for orbs, gravity portals, etc.

# 2.0.5
- fixed an issue where P2 was simulated when not in Dual Mode
- fixed an issue in MegaHack where fakeplayer was judged to be dead and hitboxes were displayed (probably)

# 2.0.3
- Support cross-platform
- Better rotation extrapolation

# 2.0.2
- Solved the problem where simulations affected actual physics.
- Fixed the issue where fake player death triggered MegaHack's death detector.

# 2.0.1
- Changed to use Silicate's physics simulation logic (to avoid affecting actual game physics)

# 2.0.0
- Refactored to mimic actual game physics (Most accurate frame extrapolation ever)

# 1.0.6
- Added soft-toggle setting

# 1.0.5
- Various optimizations

# 1.0.4
- Fixed the issue where getSettingValue was being called unnecessarily many times.

# 1.0.2
- optimized

# 1.0.0
- Initial release
