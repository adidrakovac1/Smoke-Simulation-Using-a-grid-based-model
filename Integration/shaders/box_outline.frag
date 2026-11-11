// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

out vec4 FragColor;

uniform vec3 u_Color;

void main()
{
    FragColor = vec4(u_Color, 1.0);
}
