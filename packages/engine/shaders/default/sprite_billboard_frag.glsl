#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform vec4 albedoRect;
uniform vec2 atlasSize;
uniform int useBrightmap;
uniform int usePartMask;
uniform int hiddenPartsMask;

out vec4 finalColor;

void main()
{
    vec4 texel = texture(texture0, fragTexCoord);
    float alpha = texel.a * fragColor.a;
    if (alpha < 0.5) {
        discard;
    }

    vec2 localUv = vec2(0.0);
    if (useBrightmap != 0 || usePartMask != 0) {
        float width = albedoRect.z;
        float height = albedoRect.w;
        float absW = abs(width);
        float absH = abs(height);
        vec2 atlasPx = fragTexCoord * atlasSize;
        float localX = absW > 1e-5 ? (atlasPx.x - albedoRect.x) / absW : 0.0;
        float localY = absH > 1e-5 ? (atlasPx.y - albedoRect.y) / absH : 0.0;
        if (width < 0.0) {
            localX = 1.0 - localX;
        }
        if (height < 0.0) {
            localY = 1.0 - localY;
        }
        localUv = clamp(vec2(localX, localY), 0.0, 1.0);
    }

    if (usePartMask != 0) {
        int part = int(texture(texture2, localUv).r * 255.0 + 0.5);
        if (part != 0 && ((hiddenPartsMask >> (part - 1)) & 1) != 0) {
            discard;
        }
    }

    vec3 light = fragColor.rgb;
    if (useBrightmap != 0) {
        float bright = texture(texture1, localUv).r;
        light = mix(fragColor.rgb, vec3(1.0), clamp(bright, 0.0, 1.0));
    }

    finalColor = vec4(texel.rgb * light, alpha);
}
