// ---- Binding 0: Globals (UBO everywhere) ----
layout(std140, binding = 0) uniform UBO_Globals {
    vec3 iResolution;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    int iFrame;
    int u_UseInstancing;
    ivec2 _Offset0;
    vec4 iMouse;
    vec4 iDate;
} u_Globals;

#define NOVA_iResolution u_Globals.iResolution
#define NOVA_iTime       u_Globals.iTime
#define NOVA_iTimeDelta  u_Globals.iTimeDelta
#define NOVA_iFrameRate  u_Globals.iFrameRate
#define NOVA_iFrame      u_Globals.iFrame
#define NOVA_UseInstancing u_Globals.u_UseInstancing
#define NOVA_iMouse      u_Globals.iMouse
#define NOVA_iDate       u_Globals.iDate

// ---- Binding 1: MVP (UBO everywhere) ----
layout(std140, binding = 1) uniform UBO_MVP {
    mat4 model;
    mat4 view;
    mat4 proj;
} u_MVP;

#define NOVA_MODEL u_MVP.model
#define NOVA_VIEW  u_MVP.view
#define NOVA_PROJ  u_MVP.proj

// ---- Binding 2: per-instance data (SSBO everywhere) ----
struct SSBO_InstanceData {
    mat4 model;
    vec4 color;
};

layout(std430, binding = 2) readonly buffer InstanceBuffer {
    SSBO_InstanceData instances[];
} u_Instances;

// ---- Binding 3: Material (UBO everywhere) ----
layout(std140, binding = 3) uniform UBO_Material {
    vec4 u_Color;
};
