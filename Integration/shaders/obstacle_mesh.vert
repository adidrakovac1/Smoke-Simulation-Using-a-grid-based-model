// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

// Transparent pass for porous obstacles (the "blanket"). The mesh is already in world space, in the Renderer's
// mesh vertex layout: location 0 = position, 1 = normal.
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

uniform mat4 u_ViewProj;

out vec3 v_Normal;

void main()
{
    v_Normal = a_Normal;
    gl_Position = u_ViewProj * vec4(a_Position, 1.0);
}
