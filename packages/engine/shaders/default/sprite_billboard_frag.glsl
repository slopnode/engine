#version 330

/// Draws one sprite frame out of its atlas with an optional brightmap and part mask.
///
/// `albedoRect` and `atlasSize` map the quad onto the frame's rectangle in the atlas.
/// With `useBrightmap` set, the red channel of `texture1` marks texels that ignore
/// lighting. With `usePartMask` set, `texture2` holds a part index per texel and any
/// part whose bit is set in `hiddenPartsMask` is discarded.
///
/// @used-by World sprites and first-person sprites.
/// @see sprite-hide-part! fp-set-sprite-frame

in vec2 fragTexCoord;
in vec4 fragColor;

/// Sprite atlas.
uniform sampler2D texture0;
/// Brightmap, red channel marks unlit texels.
uniform sampler2D texture1;
/// Part mask, one part index per texel.
uniform sampler2D texture2;
/// Frame rectangle in the atlas as x, y, width, height in texels.
uniform vec4 albedoRect;
/// Atlas dimensions in texels.
uniform vec2 atlasSize;
/// 1 to apply the brightmap.
uniform int useBrightmap;
/// 1 to apply the part mask.
uniform int usePartMask;
/// Bit set of hidden part indices.
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
