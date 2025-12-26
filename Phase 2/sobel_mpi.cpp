// sobel_mpi.cpp   (1D)
#include <mpi.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
using namespace std;

void make_test_image(vector<int> &img, int N) {
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            img[i*N + j] = ((i*31 + j*17) % 256);
}
int clamp255(int v){ return v < 0 ? 0 : (v>255?255:v); }

// compute interior (rows 2..local_h-1 in extended buffer)
void compute_interior(const vector<int> &buf, vector<int> &out, int N, int local_h) {
    // interior rows: 2..local_h-1 inclusive (1-based in extended, since 0 is top ghost)
    for (int i = 2; i <= local_h-1; ++i) {
        for (int j = 1; j < N-1; ++j) {
            int idx = i*N + j;
            int Gx = -buf[idx-N-1] - 2*buf[idx-1] - buf[idx+N-1]
                     + buf[idx-N+1] + 2*buf[idx+1] + buf[idx+N+1];
            int Gy = -buf[idx-N-1] - 2*buf[idx-N]   - buf[idx-N+1]
                     + buf[idx+N-1] + 2*buf[idx+N]   + buf[idx+N+1];
            int val = (int)std::sqrt((double)Gx*Gx + (double)Gy*Gy);
            out[(i-1-1)*N + j] = clamp255(val); // mapped to out: rows 0..local_h-1
        }
    }
}

// compute boundary rows (first and last real rows)
void compute_boundaries(const vector<int> &buf, vector<int> &out, int N, int local_h) {
    // first real row is at extended index 1 -> compute i=1
    int i = 1;
    for (int j = 1; j < N-1; ++j) {
        int idx = i*N + j;
        int Gx = -buf[idx-N-1] - 2*buf[idx-1] - buf[idx+N-1]
                 + buf[idx-N+1] + 2*buf[idx+1] + buf[idx+N+1];
        int Gy = -buf[idx-N-1] - 2*buf[idx-N]   - buf[idx-N+1]
                 + buf[idx+N-1] + 2*buf[idx+N]   + buf[idx+N+1];
        int val = (int)std::sqrt((double)Gx*Gx + (double)Gy*Gy);
        out[(i-1)*N + j] = clamp255(val);
    }
    // last real row is at extended index local_h
    i = local_h;
    for (int j = 1; j < N-1; ++j) {
        int idx = i*N + j;
        int Gx = -buf[idx-N-1] - 2*buf[idx-1] - buf[idx+N-1]
                 + buf[idx-N+1] + 2*buf[idx+1] + buf[idx+N+1];
        int Gy = -buf[idx-N-1] - 2*buf[idx-N]   - buf[idx-N+1]
                 + buf[idx+N-1] + 2*buf[idx+N]   + buf[idx+N+1];
        int val = (int)std::sqrt((double)Gx*Gx + (double)Gy*Gy);
        out[(i-1-1)*N + j] = clamp255(val);
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size; MPI_Comm_rank(MPI_COMM_WORLD,&rank); MPI_Comm_size(MPI_COMM_WORLD,&size);

    if (argc < 2) {
        if (rank==0) cout<<"Usage: mpirun -np P ./sobel_mpi N\n";
        MPI_Finalize(); return 0;
    }
    int N = stoi(argv[1]);
    if (N % size != 0 && rank==0) {
        cerr<<"Warning: N not divisible by P; last ranks will get smaller blocks (simple version assumes divisibility)\n";
    }
    int local_h = N / size; // rows per rank (real)
    // extended buffer includes top & bottom ghosts: size = (local_h + 2) * N
    vector<int> local_buf((local_h + 2) * N, 0);
    vector<int> local_out(local_h * N, 0);

    vector<int> full_img;
    if (rank==0) {
        full_img.assign(N*N,0);
        make_test_image(full_img, N);
    }

    // Scatter real rows (each rank receives local_h * N ints) into local_buf offset N (skip top ghost)
    MPI_Scatter(rank==0?full_img.data():nullptr, local_h*N, MPI_INT,
                &local_buf[N], local_h*N, MPI_INT, 0, MPI_COMM_WORLD);

    // Start timing (only time compute+comm)
    MPI_Barrier(MPI_COMM_WORLD);
    auto t0 = chrono::high_resolution_clock::now();

    // Nonblocking halo exchange
    MPI_Request reqs[4];
    // top receive (into row 0)
    if (rank>0)
        MPI_Irecv(&local_buf[0], N, MPI_INT, rank-1, 100, MPI_COMM_WORLD, &reqs[0]);
    else reqs[0] = MPI_REQUEST_NULL;
    // bottom receive (into last extended row)
    if (rank < size-1)
        MPI_Irecv(&local_buf[(local_h+1)*N], N, MPI_INT, rank+1, 101, MPI_COMM_WORLD, &reqs[1]);
    else reqs[1] = MPI_REQUEST_NULL;
    // top send (send first real row at offset N)
    if (rank>0)
        MPI_Isend(&local_buf[N], N, MPI_INT, rank-1, 101, MPI_COMM_WORLD, &reqs[2]);
    else reqs[2] = MPI_REQUEST_NULL;
    // bottom send (send last real row at offset local_h*N)
    if (rank < size-1)
        MPI_Isend(&local_buf[local_h*N], N, MPI_INT, rank+1, 100, MPI_COMM_WORLD, &reqs[3]);
    else reqs[3] = MPI_REQUEST_NULL;

    // Compute interior (rows 2..local_h-1)
    if (local_h > 2) compute_interior(local_buf, local_out, N, local_h);

    // Wait for halo
    MPI_Waitall(4, reqs, MPI_STATUSES_IGNORE);

    // Compute boundary rows now that halo arrived
    if (local_h >= 1) compute_boundaries(local_buf, local_out, N, local_h);

    MPI_Barrier(MPI_COMM_WORLD);
    auto t1 = chrono::high_resolution_clock::now();
    double local_ms = chrono::duration_cast<chrono::milliseconds>(t1-t0).count();

    // Gather timings to rank 0
    double max_ms;
    MPI_Reduce(&local_ms, &max_ms, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    // Gather output to full_img on rank 0
    if (rank==0) full_img.assign(N*N,0);
    MPI_Gather(local_out.data(), local_h*N, MPI_INT,
               rank==0?full_img.data():nullptr, local_h*N, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank==0) {
        cout<<"MODE=MPI N="<<N<<" P="<<size<<" time_ms="<<max_ms<<"\n";
        if (N <= 16) {
            cout<<"Output snippet:\n";
            for (int i=0;i<min(N,8);++i){
                for (int j=0;j<min(N,8);++j) cout<<full_img[i*N+j]<<" ";
                cout<<"\n";
            }
        }
    }

    MPI_Finalize();
    return 0;
}
