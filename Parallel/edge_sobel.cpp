// edge_sobel.cpp
// Usage examples:
//  ./edge_sobel seq 1024        -> sequential on 1024x1024 image
//  ./edge_sobel omp 1024 4     -> parallel with 4 threads on 1024x1024

#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <cstring>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace std;

void make_test_image(vector<int> &img, int N) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            img[i*N + j] = ((i*31 + j*17) % 256);
}

int clamp255(int v){ return v < 0 ? 0 : (v>255?255:v); }

double run_sobel_seq(const vector<int> &img, vector<int> &out, int N) {
    auto start = chrono::high_resolution_clock::now();
    for (int i = 1; i < N-1; ++i) {
        for (int j = 1; j < N-1; ++j) {
            int Gx = -img[(i-1)*N + (j-1)] - 2*img[i*N + (j-1)] - img[(i+1)*N + (j-1)]
                     + img[(i-1)*N + (j+1)] + 2*img[i*N + (j+1)] + img[(i+1)*N + (j+1)];
            int Gy = -img[(i-1)*N + (j-1)] - 2*img[(i-1)*N + j]   - img[(i-1)*N + (j+1)]
                     + img[(i+1)*N + (j-1)] + 2*img[(i+1)*N + j]   + img[(i+1)*N + (j+1)];
            int val = (int) std::sqrt((double)Gx*Gx + (double)Gy*Gy);
            out[i*N + j] = clamp255(val);
        }
    }
    auto end = chrono::high_resolution_clock::now();
    return chrono::duration_cast<chrono::milliseconds>(end - start).count();
}

double run_sobel_omp(const vector<int> &img, vector<int> &out, int N) {
    auto start = chrono::high_resolution_clock::now();
    #pragma omp parallel for schedule(static)
    for (int i = 1; i < N-1; ++i) {
        for (int j = 1; j < N-1; ++j) {
            int Gx = -img[(i-1)*N + (j-1)] - 2*img[i*N + (j-1)] - img[(i+1)*N + (j-1)]
                     + img[(i-1)*N + (j+1)] + 2*img[i*N + (j+1)] + img[(i+1)*N + (j+1)];
            int Gy = -img[(i-1)*N + (j-1)] - 2*img[(i-1)*N + j]   - img[(i-1)*N + (j+1)]
                     + img[(i+1)*N + (j-1)] + 2*img[(i+1)*N + j]   + img[(i+1)*N + (j+1)];
            int val = (int) std::sqrt((double)Gx*Gx + (double)Gy*Gy);
            out[i*N + j] = clamp255(val);
        }
    }
    auto end = chrono::high_resolution_clock::now();
    return chrono::duration_cast<chrono::milliseconds>(end - start).count();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        cout << "Usage: ./edge_sobel [seq|omp] N [threads]\n";
        return 1;
    }
    string mode = argv[1];
    int N = stoi(argv[2]);
    int threads = 1;
    if (argc >= 4) threads = stoi(argv[3]);

    vector<int> img(N*N);
    vector<int> out(N*N);
    make_test_image(img, N);

    if (mode == "seq") {
        double ms = run_sobel_seq(img, out, N);
        cout << "MODE=SEQUENTIAL N="<<N<<" time_ms="<<ms<<"\n";
    } else if (mode == "omp") {
#ifdef _OPENMP
        omp_set_num_threads(threads);
        double ms = run_sobel_omp(img, out, N);
        cout << "MODE=OPENMP N="<<N<<" threads="<<threads<<" time_ms="<<ms<<"\n";
#else
        cerr << "Not compiled with OpenMP support\n";
        return 2;
#endif
    } else {
        cerr << "Unknown mode\n";
        return 1;
    }

    if (N <= 16) {
        cout << "Output snippet:\n";
        for (int i=0;i<min(N,8);++i) {
            for (int j=0;j<min(N,8);++j) cout << out[i*N+j] << " ";
            cout << "\n";
        }
    }
    return 0;
}
