#version 450
//#define MAX_FRAGMENT_COUNT 128

#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : enable
#extension GL_GOOGLE_include_directive : require

#include "bitonic_func.glsl"
#include "shaderCommon.glsl"

struct Node
{
    vec4 color;
    float depth;
    uint next;
};

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 0, r32ui) uniform uimage2D headIndexImage;

layout (set = 0, binding = 1) buffer LinkedListSBO
{
    Node nodes[];
};

void main()
{
    //Node fragments[MAX_FRAGMENT_COUNT];
    Node fragments;
    uint64_t frag[MAX_FRAGMENT_COUNT];
    uint64_t registers[32];
    uint blockIdx[4] = {0, 32, 64, 96};
    int count = 0;

    uint nodeIdx = imageLoad(headIndexImage, ivec2(gl_FragCoord.xy)).r;

    while (nodeIdx != 0xffffffff && count < MAX_FRAGMENT_COUNT)
    {
        fragments = nodes[nodeIdx];
        frag[count] = packUint2x32(uvec2(packUnorm4x8(fragments.color), floatBitsToUint(fragments.depth)));
        nodeIdx = fragments.next;
        ++count;
    }

    uint groups = count / 32;
    // padding
    for (uint i = count; i < (groups + 1) * 32; ++i) {
        frag[i] = 0;
    }

    for (uint i = 0; i < groups + 1; ++i) {
        // load from local memory
        for (uint j = 0; j < 32; ++j) {
            registers[j] = frag[i * 32 + j];
        }
        // sort
        line(registers);
        triangle4(registers);
        line(registers);
        triangle8(registers);
        rhombus4(registers);
        line(registers);
        triangle16(registers);
        rhombus8(registers);
        rhombus4(registers);
        line(registers);
        triangle32(registers);
        rhombus16(registers);
        rhombus8(registers);
        rhombus4(registers);
        line(registers);
        // write back
        for (uint j = 0; j < 32; ++j) {
            frag[i * 32 + j] = registers[j];
        }
    }

    // merge and blending
    vec4 color = vec4(1);
    for (int i = 0; i < count; ++i)
    {
        uint64_t temp = 0;
        uint b = 0;
        for (uint j = 0; j < groups + 1; ++j) {
            if (blockIdx[j] < (j + 1) * 32 && frag[blockIdx[j]] > temp) {
                b = j;
                temp = frag[blockIdx[j]];
            }
        }
        blockIdx[b]++;
        uvec2 t = unpackUint2x32(temp);
        doBlendPacked(color, t.x);
    }
    outFragColor = color;
}