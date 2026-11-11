// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

in vec3 v_Normal;
out vec4 FragColor;

uniform vec3  u_ToLight;   // unit vector towards the light
uniform vec3  u_Color;
uniform float u_Alpha;

void main()
{
    // Simple diffuse shading plus a constant part, so the side turned away from the light is not black.
    float diffuse = max(dot(normalize(v_Normal), u_ToLight), 0.0);
    vec3 colour = u_Color * (0.25 + 0.75 * diffuse);
    // Premultiplied alpha, the same blending as the smoke.
    FragColor = vec4(colour * u_Alpha, u_Alpha);
}
