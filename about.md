# CBF Extrapolate
Extrapolate the frame with CBF.

Some codes are based on [Click-Between-Frames](https://github.com/theyareonit/Click-Between-Frames) and [Silicate](https://git.silicate.dev/silicate/silicate).

## What does this mod do?
This mod is essentially a "Visual-Only Physics Bypass". It has absolutely no effect on in-game hitboxes or physical gameplay; it simply extrapolates the player's position and rotation during rendering to make the movement look smoother.

The name "CBF Extrapolate" was chosen mostly to attract interest (as a bit of clickbait). Although it was designed to eliminate micro-stutters and maximize responsiveness when used together with Click-Between-Frames (CBF), CBF is not actually required—the mod functions perfectly fine on its own.

## How is it different from Mega Hack's interpolation?
Standard frame interpolation, such as Mega Hack's implementation, relies on simple linear extrapolation/interpolation which does not match actual game physics. This mod, however, runs real game physics updates on a fake player object during the rendering phase while incorporating CBF input timestamps. This ensures the visual movement matches actual gameplay physics, significantly reducing perceived latency and input delay.