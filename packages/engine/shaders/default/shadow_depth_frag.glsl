#version 330

in vec2 fragTexCoord;

uniform sampler2D texture0;
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
