#version 330

/// Writes depth only, discarding masked texels when alpha clipping is on.
///
/// @used-by Shadow map passes for dynamic lights.

in vec2 fragTexCoord;

/// Albedo, sampled only for the alpha test.
uniform sampler2D texture0;
/// 1 to discard texels under half alpha so masked sprites cast cut-out shadows.
uniform int useAlphaClip;

void main()
{
    if (useAlphaClip != 0) {
        float alpha = texture(texture0, fragTexCoord).a;
        if (alpha < 0.5) {
            discard;
        }
    }
}
