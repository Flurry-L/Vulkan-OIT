#version 460
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : enable
#extension GL_GOOGLE_include_directive : require
#include "shaderCommon.glsl"
//#define OIT_LAYERS 4

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 1) buffer LinkedListSBO
{
    uvec2 kbuf[];
};

layout (set = 0, binding = 2) uniform RenderPassUBO
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
    ivec3 viewport;
} renderPassUBO;

void main()
{
    const int viewSize = renderPassUBO.viewport.z;
    ivec2 coord = ivec2(gl_FragCoord.xy);
    const int listPos  = coord.y * renderPassUBO.viewport.x + coord.x;
    vec4 color = vec4(1);
    for (int i = OIT_LAYERS - 1; i >= 0; i--) {
        //uvec2 stored = kbuf[listPos + i * viewSize];
        uvec2 stored = kbuf[listPos * OIT_LAYERS + i];
        if (stored.y != 0xFFFFFFFFu) {
            doBlendPacked(color, stored.x);
        }
    }
    outFragColor = color;

    /*uvec2 stored = kbuf[listPos * OIT_LAYERS + 3];
    outFragColor = unpackUnorm4x8(stored.x);*/
}