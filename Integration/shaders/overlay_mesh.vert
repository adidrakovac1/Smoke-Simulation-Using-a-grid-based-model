// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

// D22: transparent overlays of the house scene (porous furniture, smoke source spheres, the reach of a fan).
// Like obstacle_mesh.vert, but the mesh is in its own coordinates and u_Model places it (the same T * R * S
// matrix the Renderer uses for the object). Used with obstacle_mesh.frag.
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;

uniform mat4 u_ViewProj;
uniform mat4 u_Model;

out vec3 v_Normal;

void main()
{
    // mat3(u_Model) is enough for the shading of an overlay; for a non-uniform scale the normal is only
    // approximately perpendicular, which a flat semi-transparent colour does not show.
    v_Normal = mat3(u_Model) * a_Normal;
    gl_Position = u_ViewProj * u_Model * vec4(a_Position, 1.0);
}
