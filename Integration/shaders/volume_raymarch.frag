// No #version line: the Renderer's ShaderProgram adds it (GLSL 3.30 to 4.10, from the OpenGL context).

// Volume ray marching of the smoke density (Crane, Llamas, Tariq: "Real-Time Simulation and Rendering of 3D
// Fluids", GPU Gems 3, chapter 30, section 30.3).
//
// For this pixel:
//   1. build the view ray (camera position, direction through the pixel),
//   2. find where the ray enters and leaves the box (t0, t1),
//   3. walk from t0 to t1 in u_Steps equal steps; at each sample read the density rho and
//        opacity of the step   a = 1 - exp(-u_Absorption * rho * ds)        (Beer-Lambert law)
//        colour  += T * a * (smoke colour * light reaching the sample)
//        T       *= 1 - a                                                    (T = transmittance so far)
//   4. output colour and opacity 1 - T; OpenGL blends it over the background.

in vec2 v_Ndc;
out vec4 FragColor;

uniform sampler3D u_Density;
uniform mat4  u_InvViewProj;   // clip space -> world space
uniform vec3  u_CameraPos;
uniform vec3  u_BoxMin;        // world-space box that the grid fills
uniform vec3  u_BoxSize;
uniform int   u_Steps;
uniform int   u_LightSteps;    // 0 = no self-shadowing
uniform float u_Absorption;
uniform float u_ShadowStrength;
uniform float u_Ambient;
uniform vec3  u_SmokeColor;
uniform vec3  u_ToLight;       // unit vector towards the light
uniform vec3  u_LightColor;
uniform float u_Jitter;        // 1 = shift the first sample per pixel
uniform sampler2D u_SceneDepth;   // depth of the opaque scene (decision D13)
uniform bool  u_UseSceneDepth;

const int MAX_STEPS = 512;
const int MAX_LIGHT_STEPS = 32;

// World position -> texture coordinates in [0,1]^3. Texel centres are cell centres, and the box faces are the
// walls of the simulation, so no extra offset is needed.
vec3 toBox(vec3 world) { return (world - u_BoxMin) / u_BoxSize; }

float densityAt(vec3 uvw) { return max(texture(u_Density, uvw).r, 0.0); }

// Ray / axis-aligned box intersection ("slab" method): the ray is inside the box where it is between the two
// planes of every axis at the same time.
bool intersectBox(vec3 ro, vec3 rd, out float t0, out float t1)
{
    vec3 inv = 1.0 / rd;
    vec3 a = (u_BoxMin - ro) * inv;
    vec3 b = (u_BoxMin + u_BoxSize - ro) * inv;
    vec3 tMin = min(a, b);
    vec3 tMax = max(a, b);
    t0 = max(max(tMin.x, tMin.y), tMin.z);
    t1 = min(min(tMax.x, tMax.y), tMax.z);
    t0 = max(t0, 0.0);              // the camera may be inside the box
    return t1 > t0;
}

// How much light reaches point p: march towards the light and add up the density on the way.
float lightReaching(vec3 p)
{
    if (u_LightSteps <= 0) return 1.0;
    float stepLength = 0.5 * length(u_BoxSize) / float(u_LightSteps);
    float opticalDepth = 0.0;
    for (int j = 0; j < MAX_LIGHT_STEPS; ++j) {
        if (j >= u_LightSteps) break;
        vec3 uvw = toBox(p + u_ToLight * (float(j) + 0.5) * stepLength);
        if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) break;  // left the box
        opticalDepth += densityAt(uvw) * stepLength;
    }
    return exp(-u_Absorption * u_ShadowStrength * opticalDepth);
}

void main()
{
    vec4 farPoint = u_InvViewProj * vec4(v_Ndc, 1.0, 1.0);
    vec3 rd = normalize(farPoint.xyz / farPoint.w - u_CameraPos);
    vec3 ro = u_CameraPos;

    float t0, t1;
    if (!intersectBox(ro, rd, t0, t1)) discard;

    // Opaque objects hide the smoke behind them: read the depth the Renderer wrote for this pixel, turn it back
    // into a world position and cut the ray there. Depth 1.0 means "nothing was drawn here".
    if (u_UseSceneDepth) {
        float depth = texture(u_SceneDepth, gl_FragCoord.xy / vec2(textureSize(u_SceneDepth, 0))).r;
        if (depth < 1.0) {
            vec4 scenePoint = u_InvViewProj * vec4(v_Ndc, depth * 2.0 - 1.0, 1.0);
            t1 = min(t1, dot(scenePoint.xyz / scenePoint.w - ro, rd));
            if (t1 <= t0) discard;   // the object is in front of the box
        }
    }

    float ds = (t1 - t0) / float(u_Steps);
    // A different sub-step offset for neighbouring pixels turns visible "rings" into fine noise.
    float offset = 0.5;
    if (u_Jitter > 0.5)
        offset = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    float transmittance = 1.0;
    vec3 colour = vec3(0.0);
    for (int i = 0; i < MAX_STEPS; ++i) {
        if (i >= u_Steps) break;
        vec3 p = ro + rd * (t0 + (float(i) + offset) * ds);
        float rho = densityAt(toBox(p));
        if (rho > 1e-4) {
            float a = 1.0 - exp(-u_Absorption * rho * ds);
            vec3 radiance = u_SmokeColor * (vec3(u_Ambient) + u_LightColor * lightReaching(p));
            colour += transmittance * a * radiance;
            transmittance *= 1.0 - a;
            if (transmittance < 0.01) break;   // nothing behind this point is visible any more
        }
    }

    FragColor = vec4(colour, 1.0 - transmittance);
}
