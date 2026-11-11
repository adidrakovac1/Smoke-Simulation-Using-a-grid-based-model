// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

// Corner of the unit cube [0,1]^3, placed into the world-space box of the simulation.
layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProj;
uniform vec3 u_BoxMin;
uniform vec3 u_BoxSize;

void main()
{
    gl_Position = u_ViewProj * vec4(u_BoxMin + a_Position * u_BoxSize, 1.0);
}
