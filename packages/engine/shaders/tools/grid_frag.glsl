#version 330

in vec3 fragWorldPos;

uniform vec3 cameraPos;
uniform float gridSize;
uniform int planeAxis;
uniform float fadeRadius;
uniform vec4 minorColor;
uniform vec4 majorColor;

out vec4 finalColor;

vec2 planeUv(vec3 p)
{
    if (planeAxis == 1) {
        return p.xy;
    }
    if (planeAxis == 2) {
        return p.yz;
    }
    return p.xz;
}

float lineMask(vec2 uv, float spacing)
{
    vec2 coord = uv / max(spacing, 1e-8);
    vec2 deriv = max(fwidth(coord), vec2(1e-6));
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / deriv;
    float line = min(grid.x, grid.y);
    return 1.0 - min(line, 1.0);
}

float levelFade(float spacing, float metersPerPixel)
{
    float pixels = spacing / max(metersPerPixel, 1e-8);
    return smoothstep(1.25, 3.5, pixels);
}

void main()
{
    vec2 uv = planeUv(fragWorldPos);
    float mpp = length(fwidth(uv));
    float cell = max(gridSize, 1e-6);

    float a0 = lineMask(uv, cell) * levelFade(cell, mpp) * minorColor.a;
    float a1 = lineMask(uv, cell * 10.0) * levelFade(cell * 10.0, mpp) * majorColor.a;
    float a2 = lineMask(uv, cell * 100.0) * levelFade(cell * 100.0, mpp) * majorColor.a;

    float alpha = max(a0, max(a1, a2));
    if (alpha < 0.004) {
        discard;
    }

    vec3 color = minorColor.rgb;
    if (a1 >= a0 && a1 >= a2) {
        color = majorColor.rgb;
    } else if (a2 >= a0 && a2 >= a1) {
        color = mix(majorColor.rgb, vec3(0.55, 0.58, 0.64), 0.35);
    }

    float dist = length(uv - planeUv(cameraPos));
    float fade = 1.0 - smoothstep(fadeRadius * 0.55, fadeRadius, dist);
    alpha *= fade;
    if (alpha < 0.004) {
        discard;
    }

    finalColor = vec4(color, alpha);
}
