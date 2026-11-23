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

__global__ void spillingKernel(float *out, float *in, float *in2) {
    // Large array to force register spilling
    float temp[64]; // Adjust size based on register pressure

    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= N) return;

    // Prevent compiler optimizations
    #pragma unroll 1
    for (int i = 0; i < 64; i++) {
        temp[i] = in[idx] * in2[idx] * (i + 1) * 0.001f;
    }

    float sum = 0.0f;
    for (int i = 0; i < 64; i++) {
        sum += temp[i] * temp[(i + 1) % 64];
    }

    out[idx] = sum;
}

// Kernel with higher register usage through vector math
__global__ void vectorKernel(float *out, float *in) {
    float temp[72]; // Slightly larger for different spilling pattern

    int idx = threadIdx.x + blockIdx.x * blockDim.x;
    if (idx >= N) return;

    // Polynomial evaluation to increase register pressure
#pragma unroll 2
    for (int i = 0; i < 72; i++) {
        float x = in[idx] + i * 0.01f;
        temp[i] = x * x * 0.5f + x * 1.2f + 3.4f * sinf(x * 0.1f);
    }

    float sum1 = 0.0f, sum2 = 0.0f;
#pragma unroll 1
    for (int i = 0; i < 36; i++) {
        sum1 += temp[i] * temp[71 - i];
        sum2 += temp[i + 36] * temp[35 - i];
    }

    out[idx] = sum1 + sum2 * 0.7f;
}


int main() {
    float *d_in, *d_in2, *d_out1, *d_out2, *d_out3;
    float *h_in, *h2_in, *h_out1, *h_out2, *h_out3;

    h_in = new float[N];
    h2_in = new float[N];
    h_out1 = new float[N];
    h_out2 = new float[N];
    h_out3 = new float[N];

    for (int i = 0; i < N; i++) {
        h_in[i] = static_cast<float>(i % 256);
        h2_in[i] = static_cast<float>(i % 256);
    }

    hipMalloc(&d_in2, N * sizeof(float));
    hipMalloc(&d_in, N * sizeof(float));
    hipMalloc(&d_out1, N * sizeof(float));
    hipMalloc(&d_out2, N * sizeof(float));
    hipMalloc(&d_out3, N * sizeof(float));

    hipMemcpy(d_in, h_in, N * sizeof(float), hipMemcpyHostToDevice);
    hipMemcpy(d_in2, h2_in, N * sizeof(float), hipMemcpyHostToDevice);

    dim3 blocks(32);
    dim3 threads(32);

    // Launch all kernels sequentially
    spillingKernel<<<blocks, threads>>>(d_out1, d_in);
    hipDeviceSynchronize();

    spillingKernel<<<blocks, threads>>>(d_out3, d_in, d_in2);
    hipDeviceSynchronize();

    vectorKernel<<<blocks, threads>>>(d_out2, d_in);
    hipDeviceSynchronize();

    hipMemcpy(h_out1, d_out1, N * sizeof(float), hipMemcpyDeviceToHost);
    hipMemcpy(h_out2, d_out2, N * sizeof(float), hipMemcpyDeviceToHost);
    hipMemcpy(h_out3, d_out3, N * sizeof(float), hipMemcpyDeviceToHost);


    std::cout << "Spilling kernel result[0]: " << h_out1[0] << std::endl;
    std::cout << "Vector kernel result[0]: " << h_out2[0] << std::endl;
    std::cout << "Spilling kernel 2 result[0]: " << h_out3[0] << std::endl;

    delete[] h_in;
    delete[] h2_in;
    delete[] h_out1;
    delete[] h_out2;
    delete[] h_out3;
    hipFree(d_in);
    hipFree(d_in2);
    hipFree(d_out1);
    hipFree(d_out2);
    hipFree(d_out3);

    return 0;
}
