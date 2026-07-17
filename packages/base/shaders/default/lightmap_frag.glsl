#version 330

in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec4 colDiffuse;
uniform vec4 colSpecular;
uniform int useLightmap;

out vec4 finalColor;

void main()
{
    vec4 albedo = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 light = useLightmap != 0 ? texture(texture1, fragTexCoord2).rgb : vec3(1.0);
    vec3 emit = colSpecular.rgb;
    finalColor = vec4(albedo.rgb * light + emit, albedo.a);
}
