#include <hip/hip_runtime.h>
#include <iostream>

#define N 1024

__global__ void spillingKernel(float *out, float *in) {
    // Large array to force register spilling
    float temp[64]; // Adjust size based on register pressure

    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= N) return;

    // Prevent compiler optimizations
    #pragma unroll 1
    for (int i = 0; i < 64; i++) {
        temp[i] = in[idx] * (i + 1) * 0.001f;
    }

    float sum = 0.0f;
    for (int i = 0; i < 64; i++) {
        sum += temp[i] * temp[(i + 1) % 64];
    }

    out[idx] = sum;
}

int main() {
    float *d_in, *d_out;
    float *h_in, *h_out;

    h_in = new float[N];
    h_out = new float[N];

    for (int i = 0; i < N; i++) h_in[i] = static_cast<float>(i);

    hipMalloc(&d_in, N * sizeof(float));
    hipMalloc(&d_out, N * sizeof(float));

    hipMemcpy(d_in, h_in, N * sizeof(float), hipMemcpyHostToDevice);

    dim3 blocks(32);
    dim3 threads(32);

    spillingKernel<<<blocks, threads>>>(d_out, d_in);
    hipDeviceSynchronize();

    hipMemcpy(h_out, d_out, N * sizeof(float), hipMemcpyDeviceToHost);

    std::cout << "Result[0]: " << h_out[0] << std::endl;

    delete[] h_in;
    delete[] h_out;
    hipFree(d_in);
    hipFree(d_out);

    return 0;
}

