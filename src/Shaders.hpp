// LeviFSR AI - GLSL ES 3.00 shader sources.
//
// The reconstruction pass is an independent implementation inspired by the
// published ideas behind AMD FidelityFX Super Resolution (edge adaptive
// spatial reconstruction + contrast adaptive sharpening). It is NOT AMD code
// and it is not FSR 1/2/3/4 compatible: no FSR SDK code, no FSR shaders and no
// FSR quality presets are used here.
//
//  * `kFullscreenVertexShader` draws a single fullscreen triangle without any
//    vertex buffer (gl_VertexID based), which keeps the pass free of
//    per-frame allocations.
//  * `kReconstructionFragmentShader` implements
//      - an edge adaptive spatial reconstruction filter (directional, edge
//        aligned sampling) used to rebuild detail from the low resolution
//        scene target,
//      - a contrast adaptive sharpening filter with local min/max clamping so
//        the pass cannot produce halos or ringing,
//      - a detail recovery stage that adds the high frequency information
//        which the game's own bilinear upscale removed.

#pragma once

namespace levifsr::shaders {

inline constexpr const char *kFullscreenVertexShader = R"(#version 300 es
// Fullscreen triangle generated from gl_VertexID, no vertex buffer required.
out vec2 vUv;
void main() {
  vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUv = corner;
  gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

inline constexpr const char *kReconstructionFragmentShader = R"(#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSource;      // Presented frame captured from the swap chain.
uniform sampler2D uDetail;      // Low resolution scene target (detail source).
uniform vec2 uOutputSize;       // Size of the presented frame in pixels.
uniform vec2 uSourceSize;       // Size of the captured presented frame.
uniform vec2 uDetailSize;       // Size of the low resolution detail source.
uniform float uSharpness;       // 0..1 sharpening strength.
uniform float uDetailStrength;  // 0..1 detail recovery strength.
uniform int uUseDetail;         // 1 when a low resolution source is bound.

const float kSharpnessPeak = 0.25;

float luma(vec3 c) {
  return dot(c, vec3(0.299, 0.587, 0.114));
}

// ---------------------------------------------------------------------------
// Edge adaptive spatial reconstruction.
//
// The filter estimates the local edge direction from the diagonal neighbours
// and samples along that direction, so gradients are not blurred across while
// flat areas keep their smoothness. This is the same family of idea as the
// edge adaptive spatial filter used by modern upscalers.
// ---------------------------------------------------------------------------
vec3 edgeAdaptiveSample(sampler2D tex, vec2 uv, vec2 texel) {
  vec3 e  = texture(tex, uv).rgb;
  vec3 nw = texture(tex, uv + vec2(-texel.x,  texel.y)).rgb;
  vec3 ne = texture(tex, uv + vec2( texel.x,  texel.y)).rgb;
  vec3 sw = texture(tex, uv + vec2(-texel.x, -texel.y)).rgb;
  vec3 se = texture(tex, uv + vec2( texel.x, -texel.y)).rgb;

  // Direction of the dominant gradient (diagonal differences).
  vec2 dir = vec2(luma(ne) - luma(nw), luma(se) - luma(sw));
  float len = length(dir);
  dir = len > 1e-5 ? dir / len : vec2(0.0);

  // Sample the two neighbours that lie along the detected edge direction.
  vec3 alongA = texture(tex, uv + dir * texel).rgb;
  vec3 alongB = texture(tex, uv - dir * texel).rgb;

  // Blend weight follows the local contrast: flat areas stay bilinear, edges
  // use the directional pair.
  float contrast = abs(luma(ne) - luma(sw)) + abs(luma(nw) - luma(se));
  float blend = clamp(contrast * 8.0, 0.0, 1.0);

  vec3 directional = mix(e, (alongA + alongB) * 0.5, blend * 0.75);
  return directional;
}

// ---------------------------------------------------------------------------
// Contrast adaptive sharpening.
//
// Unsharp masking whose strength is reduced on strong edges and whose result
// is clamped to the local luminance range, which removes the halo artefacts
// a naive unsharp mask produces around silhouettes.
// ---------------------------------------------------------------------------
vec3 contrastAdaptiveSharpen(sampler2D tex, vec2 uv, vec2 texel, float amount) {
  vec3 c = texture(tex, uv).rgb;

  vec3 n  = texture(tex, uv + vec2(0.0,  texel.y)).rgb;
  vec3 s  = texture(tex, uv + vec2(0.0, -texel.y)).rgb;
  vec3 w  = texture(tex, uv + vec2(-texel.x, 0.0)).rgb;
  vec3 e  = texture(tex, uv + vec2( texel.x, 0.0)).rgb;
  vec3 nw = texture(tex, uv + vec2(-texel.x,  texel.y)).rgb;
  vec3 ne = texture(tex, uv + vec2( texel.x,  texel.y)).rgb;
  vec3 sw = texture(tex, uv + vec2(-texel.x, -texel.y)).rgb;
  vec3 se = texture(tex, uv + vec2( texel.x, -texel.y)).rgb;

  float lc = luma(c);
  float lmin = min(lc, min(min(luma(n), luma(s)), min(min(luma(w), luma(e)),
                                              min(min(luma(nw), luma(ne)),
                                                  min(luma(sw), luma(se))))));
  float lmax = max(lc, max(max(luma(n), luma(s)), max(max(luma(w), luma(e)),
                                              max(max(luma(nw), luma(ne)),
                                                  max(luma(sw), luma(se))))));

  // High frequency term: centre minus the average of the neighbours.
  vec3 average = (n + s + w + e + nw + ne + sw + se) * (1.0 / 8.0);
  vec3 detail = c - average;

  // Edge aware strength: strong luminance differences get less sharpening.
  float range = lmax - lmin;
  float edgeGuard = 1.0 - smoothstep(0.10, 0.45, range);
  float strength = amount * edgeGuard;

  vec3 sharpened = c + detail * strength;

  // Clamp the result into the local range to avoid halos and ringing.
  float ls = luma(sharpened);
  float clamped = clamp(ls, lmin, lmax);
  sharpened += (clamped - ls);

  return mix(c, sharpened, clamp(amount * 1.6, 0.0, 1.0));
}

void main() {
  vec2 uv = vUv;
  vec2 sourceTexel = 1.0 / max(uSourceSize, vec2(1.0));
  vec2 detailTexel = 1.0 / max(uDetailSize, vec2(1.0));

  vec3 color = texture(uSource, uv).rgb;

  if (uUseDetail == 1) {
    // Rebuild the low resolution image with the edge adaptive filter and use
    // the difference to the plain bilinear result as high frequency detail.
    vec2 detailUv = uv;
    vec3 plain = texture(uDetail, detailUv).rgb;
    vec3 reconstructed = edgeAdaptiveSample(uDetail, detailUv, detailTexel);
    vec3 recovered = reconstructed - plain;

    // Only trust the recovered detail where it is consistent, otherwise the
    // upscale produced a different image and the difference is not detail.
    float consistency = 1.0 - smoothstep(0.02, 0.20, length(recovered));
    color += recovered * uDetailStrength * mix(0.35, 1.0, consistency);
  }

  if (uSharpness > 0.001) {
    color = contrastAdaptiveSharpen(uSource, uv, sourceTexel, uSharpness);
  }

  fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

} // namespace levifsr::shaders
