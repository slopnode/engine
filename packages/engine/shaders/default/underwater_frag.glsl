#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 resolution;
uniform float time;

uniform vec3 tint;
uniform float wobble;
uniform float vignette;
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
