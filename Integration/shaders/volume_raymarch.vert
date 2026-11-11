// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

// One triangle that covers the whole viewport, built from the vertex index (no vertex buffer needed):
// index 0 -> (-1,-1), 1 -> (3,-1), 2 -> (-1,3). The parts outside the screen are clipped away.
out vec2 v_Ndc;

void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_Ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_Ndc, 0.0, 1.0);
}
