#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : enable
void CAS(inout uint64_t z0, inout uint64_t z1) {
    if (z0 < z1) {
        uint64_t temp = z0;
        z0 = z1;
        z1 = temp;
    }
}

void line(inout uint64_t z[32]) {
    CAS(z[0], z[1]);
    CAS(z[2], z[3]);
    CAS(z[4], z[5]);
    CAS(z[6], z[7]);
    CAS(z[8], z[9]);
    CAS(z[10], z[11]);
    CAS(z[12], z[13]);
    CAS(z[14], z[15]);
    CAS(z[16], z[17]);
    CAS(z[18], z[19]);
    CAS(z[20], z[21]);
    CAS(z[22], z[23]);
    CAS(z[24], z[25]);
    CAS(z[26], z[27]);
    CAS(z[28], z[29]);
    CAS(z[30], z[31]);
}
void triangle4(inout uint64_t z[32]) {
    CAS(z[0], z[3]);
    CAS(z[4], z[7]);
    CAS(z[8], z[11]);
    CAS(z[12], z[15]);
    CAS(z[16], z[19]);
    CAS(z[20], z[23]);
    CAS(z[24], z[27]);
    CAS(z[28], z[31]);
    CAS(z[1], z[2]);
    CAS(z[5], z[6]);
    CAS(z[9], z[10]);
    CAS(z[13], z[14]);
    CAS(z[17], z[18]);
    CAS(z[21], z[22]);
    CAS(z[25], z[26]);
    CAS(z[29], z[30]);
}
void triangle8(inout uint64_t z[32]) {
    CAS(z[0], z[7]);
    CAS(z[8], z[15]);
    CAS(z[16], z[23]);
    CAS(z[24], z[31]);
    CAS(z[1], z[6]);
    CAS(z[9], z[14]);
    CAS(z[17], z[22]);
    CAS(z[25], z[30]);
    CAS(z[2], z[5]);
    CAS(z[10], z[13]);
    CAS(z[18], z[21]);
    CAS(z[26], z[29]);
    CAS(z[3], z[4]);
    CAS(z[11], z[12]);
    CAS(z[19], z[20]);
    CAS(z[27], z[28]);
}
void triangle16(inout uint64_t z[32]) {
    CAS(z[0], z[15]);
    CAS(z[16], z[31]);
    CAS(z[1], z[14]);
    CAS(z[17], z[30]);
    CAS(z[2], z[13]);
    CAS(z[18], z[29]);
    CAS(z[3], z[12]);
    CAS(z[19], z[28]);
    CAS(z[4], z[11]);
    CAS(z[20], z[27]);
    CAS(z[5], z[10]);
    CAS(z[21], z[26]);
    CAS(z[6], z[9]);
    CAS(z[22], z[25]);
    CAS(z[7], z[8]);
    CAS(z[23], z[24]);
}
void triangle32(inout uint64_t z[32]) {
    CAS(z[0], z[31]);
    CAS(z[1], z[30]);
    CAS(z[2], z[29]);
    CAS(z[3], z[28]);
    CAS(z[4], z[27]);
    CAS(z[5], z[26]);
    CAS(z[6], z[25]);
    CAS(z[7], z[24]);
    CAS(z[8], z[23]);
    CAS(z[9], z[22]);
    CAS(z[10], z[21]);
    CAS(z[11], z[20]);
    CAS(z[12], z[19]);
    CAS(z[13], z[18]);
    CAS(z[14], z[17]);
    CAS(z[15], z[16]);
}
void rhombus4(inout uint64_t z[32]) {
    CAS(z[0], z[2]);
    CAS(z[4], z[6]);
    CAS(z[8], z[10]);
    CAS(z[12], z[14]);
    CAS(z[16], z[18]);
    CAS(z[20], z[22]);
    CAS(z[24], z[26]);
    CAS(z[28], z[30]);
    CAS(z[1], z[3]);
    CAS(z[5], z[7]);
    CAS(z[9], z[11]);
    CAS(z[13], z[15]);
    CAS(z[17], z[19]);
    CAS(z[21], z[23]);
    CAS(z[25], z[27]);
    CAS(z[29], z[31]);
}
void rhombus8(inout uint64_t z[32]) {
    CAS(z[0], z[4]);
    CAS(z[8], z[12]);
    CAS(z[16], z[20]);
    CAS(z[24], z[28]);
    CAS(z[1], z[5]);
    CAS(z[9], z[13]);
    CAS(z[17], z[21]);
    CAS(z[25], z[29]);
    CAS(z[2], z[6]);
    CAS(z[10], z[14]);
    CAS(z[18], z[22]);
    CAS(z[26], z[30]);
    CAS(z[3], z[7]);
    CAS(z[11], z[15]);
    CAS(z[19], z[23]);
    CAS(z[27], z[31]);
}
void rhombus16(inout uint64_t z[32]) {
    CAS(z[0], z[8]);
    CAS(z[16], z[24]);
    CAS(z[1], z[9]);
    CAS(z[17], z[25]);
    CAS(z[2], z[10]);
    CAS(z[18], z[26]);
    CAS(z[3], z[11]);
    CAS(z[19], z[27]);
    CAS(z[4], z[12]);
    CAS(z[20], z[28]);
    CAS(z[5], z[13]);
    CAS(z[21], z[29]);
    CAS(z[6], z[14]);
    CAS(z[22], z[30]);
    CAS(z[7], z[15]);
    CAS(z[23], z[31]);
}
