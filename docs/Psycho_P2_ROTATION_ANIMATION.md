# Rotation animation increment

A single 2D `rotate()` or `rotateZ()` can now be interpolated by CSS
keyframes and transitions, including `none` as the identity and angles in
`deg`, `grad`, `rad` and `turn`. This covers the reference spinner's full
rotation without collapsing a complete turn to zero during interpolation.
Unsupported transform combinations use discrete interpolation; this is not a
CSS transform-list or matrix-decomposition implementation.

Keyframe sampling now applies timing once per segment, uses the underlying
style at missing endpoints, and preserves the final direction and fractional
position of finite alternate iterations. Reduced-motion continues to suppress
the engine's CSS animation sampling. Native tests cover quarter-turn,
implicit endpoints, units, easing, an alternate fill, transition, ticking and
reduced motion. The pixel contract independently checks the rotated rectangle
and its reduced-motion counterpart; it is not a full screenshot comparison.

Linux GCC validation: 1,171 engine / 3,018 total native checks, seven
image/paint contracts, seven DOM/layout contracts, focus and responsive-image
contracts, 18 owned goldens and 24 independent goldens passed.

This is not the requested performance architecture: each changing transform
still changes the layout signature and runs full-document layout. The native
regression explicitly observes that current behavior. There are no retained
independent compositor layer surfaces here, and the shell's dropped-frame
figures remain estimates. The remaining image/SVG and favicon paths, text
shaping/wrapping/font/DPI work, general transform animation, and other animation
semantics remain open. Existing shaping uses DirectWrite on Windows; Linux has
no glyph raster backend, and no new physical DPI validation is claimed here.
