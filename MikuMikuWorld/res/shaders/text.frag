#version 330 core

in vec2 uv1;
in vec4 color;

out vec4 fragColor;

uniform sampler2D atlas;

void main()
{
    float coverage = texture(atlas, uv1).r;
    fragColor = vec4(color.rgb, color.a * coverage);
}
