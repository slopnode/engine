#version 330

/// Full-screen underwater look: ripple, tint and vignette.
///
/// The engine runs this pass while the player is submerged and fades `amount` in and
/// out. The distortion tapers to zero near the screen edge so samples never leave the
/// render target.
///
/// @used-by The engine while the player is submerged.
/// @see actor-submerged?

in vec2 fragTexCoord;
in vec4 fragColor;

/// The rendered scene.
uniform sampler2D texture0;
/// Draw tint.
uniform vec4 colDiffuse;
/// Render target size in pixels, keeps the vignette round.
uniform vec2 resolution;
/// Seconds, drives the ripple.
uniform float time;

/// Water color.
uniform vec3 tint;
/// Ripple strength.
uniform float wobble;
/// Edge darkening strength.
uniform float vignette;
/// Overall blend from 0 to 1.
uniform float amount;

out vec4 finalColor;

void main()
{
    float amt = clamp(amount, 0.0, 1.0);

    // Taper the distortion to zero within a margin of the screen edge so the
    // sample never wanders outside the render target — otherwise the wrapped/
    // clamped edge pixels show up as visible seams and holes, most noticeable
    // eating into the first-person viewmodel near the bottom of the screen.
    const float kEdgeMargin = 0.06;
    vec2 edgeDist = min(fragTexCoord, 1.0 - fragTexCoord);
    float edgeFade = smoothstep(0.0, kEdgeMargin, min(edgeDist.x, edgeDist.y));

    vec2 uv = fragTexCoord;
    float wobbleAmp = 0.006 * wobble * amt * edgeFade;
    uv.x += sin(uv.y * 40.0 + time * 1.6) * wobbleAmp;
    uv.y += cos(uv.x * 40.0 + time * 1.3) * wobbleAmp;
    uv = clamp(uv, vec2(0.0), vec2(1.0));

    vec3 sceneColor = texture(texture0, uv).rgb;
    vec3 tinted = mix(sceneColor, sceneColor * (tint * 1.6) + tint * 0.08, amt);

    vec2 centered = fragTexCoord - 0.5;
    float aspect = resolution.x / max(resolution.y, 1.0);
    float dist = length(centered * vec2(aspect, 1.0));
    float vignetteFactor = 1.0 - smoothstep(0.35, 0.9, dist) * vignette * amt;

    finalColor = vec4(tinted * vignetteFactor, 1.0) * colDiffuse * fragColor;
}
