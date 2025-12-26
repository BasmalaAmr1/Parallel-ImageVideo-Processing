// sobel_mpi_2d.cpp  (SAFE AND CORRECT VERSION)
#include <mpi.h>
#include <iostream>
#include <vector>
#include <cmath>
using namespace std;

inline int clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

void make_test_image(vector<int> &img, int N) {
    for(int i=0;i<N;i++)
        for(int j=0;j<N;j++)
            img[i*N+j] = ((i*31 + j*17) % 256);
}

int main(int argc,char**argv)
{
    MPI_Init(&argc,&argv);

    int rank,size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);

    if(argc < 3) {
        if(rank==0)
            cout<<"Usage: mpirun -np P ./sobel_mpi_2d N Pr\n"
                <<"Pc = P/Pr automatically\n";
        MPI_Finalize();
        return 0;
    }

    int N = stoi(argv[1]);
    int Pr = stoi(argv[2]);
    int Pc = size / Pr;
    if(Pr*Pc != size) {
        if(rank==0) cerr<<"Pr*Pc != P\n";
        MPI_Finalize();
        return 0;
    }

    // 2D coordinates
    int r = rank / Pc;
    int c = rank % Pc;

    // local tile size
    int Nloc_r = N / Pr;
    int Nloc_c = N / Pc;

    int stride = Nloc_c + 2;  // <--- stride of extended tile

    // full image only on rank 0
    vector<int> full_img;
    if(rank==0){
        full_img.assign(N*N,0);
        make_test_image(full_img,N);
    }

    // === Allocate local tile with halos ===
    vector<int> tile((Nloc_r+2)*(Nloc_c+2),0);
    vector<int> out((Nloc_r)*(Nloc_c),0);

    // === Scatter manually using Scatterv of blocks ===
    vector<int> sendcounts, displs;
    if(rank==0){
        sendcounts.resize(size);
        displs.resize(size);

        for(int rr=0; rr<Pr; rr++){
            for(int cc=0; cc<Pc; cc++){
                int id = rr*Pc + cc;
                sendcounts[id] = 1;
                displs[id] = rr*Nloc_r*N + cc*Nloc_c;
            }
        }
    }

    // Subarray datatype: local_r x local_c block inside NxN
    MPI_Datatype block, block2;
    int bigN[2] = {N,N};
    int smallN[2] = {Nloc_r,Nloc_c};
    int start[2] = {0,0};

    MPI_Type_create_subarray(2,bigN,smallN,start,MPI_ORDER_C,MPI_INT,&block);
    MPI_Type_create_resized(block,0,sizeof(int),&block2);
    MPI_Type_commit(&block2);

    // Scatter each block into tile[1..Nloc_r][1..Nloc_c]
    MPI_Scatterv(
        rank==0?full_img.data():NULL,
        sendcounts.data(), displs.data(), block2,
        &tile[stride + 1],      // FIRST interior row and col
        Nloc_r*Nloc_c, MPI_INT,
        0, MPI_COMM_WORLD
    );

    MPI_Type_free(&block2);

    // === Determine neighbors ===
    int up    = (r>0    ? (r-1)*Pc + c : MPI_PROC_NULL);
    int down  = (r<Pr-1 ? (r+1)*Pc + c : MPI_PROC_NULL);
    int leftN = (c>0    ? r*Pc + (c-1) : MPI_PROC_NULL);
    int rightN= (c<Pc-1 ? r*Pc + (c+1) : MPI_PROC_NULL);

    // === HALO BUFFERS ===
    vector<int> send_top(Nloc_c), send_bottom(Nloc_c);
    vector<int> recv_top(Nloc_c), recv_bottom(Nloc_c);

    vector<int> send_left(Nloc_r), send_right(Nloc_r);
    vector<int> recv_left(Nloc_r), recv_right(Nloc_r);

    // prepare rows
    for(int j=0;j<Nloc_c;j++){
        send_top[j]    = tile[stride + (1+j)];
        send_bottom[j] = tile[(Nloc_r)*stride + (1+j)];
    }

    // prepare columns
    for(int i=0;i<Nloc_r;i++){
        send_left[i]  = tile[(1+i)*stride + 1];
        send_right[i] = tile[(1+i)*stride + (Nloc_c)];
    }

    // === NONBLOCKING EXCHANGE ===
    MPI_Request req[8];
    int k=0;

    // receives
    MPI_Irecv(recv_top.data(),    Nloc_c, MPI_INT, up,    10, MPI_COMM_WORLD, &req[k++]);
    MPI_Irecv(recv_bottom.data(), Nloc_c, MPI_INT, down,  11, MPI_COMM_WORLD, &req[k++]);
    MPI_Irecv(recv_left.data(),   Nloc_r, MPI_INT, leftN, 12, MPI_COMM_WORLD, &req[k++]);
    MPI_Irecv(recv_right.data(),  Nloc_r, MPI_INT, rightN,13, MPI_COMM_WORLD, &req[k++]);

    // sends
    MPI_Isend(send_top.data(),    Nloc_c, MPI_INT, up,    11, MPI_COMM_WORLD, &req[k++]);
    MPI_Isend(send_bottom.data(), Nloc_c, MPI_INT, down,  10, MPI_COMM_WORLD, &req[k++]);
    MPI_Isend(send_left.data(),   Nloc_r, MPI_INT, leftN, 13, MPI_COMM_WORLD, &req[k++]);
    MPI_Isend(send_right.data(),  Nloc_r, MPI_INT, rightN,12, MPI_COMM_WORLD, &req[k++]);

    // === COMPUTE INTERIOR (no halos required) ===

    for(int i=2;i<=Nloc_r-1;i++){
        for(int j=2;j<=Nloc_c-1;j++){
            int idx = i*stride + j;

            int Gx = -tile[idx-stride-1] - 2*tile[idx-1] - tile[idx+stride-1]
                     + tile[idx-stride+1] + 2*tile[idx+1] + tile[idx+stride+1];

            int Gy = -tile[idx-stride-1] - 2*tile[idx-stride] - tile[idx-stride+1]
                     + tile[idx+stride-1] + 2*tile[idx+stride] + tile[idx+stride+1];

            out[(i-2)*Nloc_c + (j-2)] = clamp255(sqrt((double)Gx*Gx + Gy*Gy));
        }
    }

    // === WAIT FOR HALOS ===
    MPI_Waitall(k,req,MPI_STATUSES_IGNORE);

    // === INSERT RECEIVED HALOS INTO tile ===
    for(int j=0;j<Nloc_c;j++){
        tile[1 + j + 0*stride] = recv_top[j];                  // row 0
        tile[1 + j + (Nloc_r+1)*stride] = recv_bottom[j];      // row Nloc_r+1
    }

    for(int i=0;i<Nloc_r;i++){
        tile[(1+i)*stride + 0] = recv_left[i];                 // col 0
        tile[(1+i)*stride + (Nloc_c+1)] = recv_right[i];       // col Nloc_c+1
    }

    // === COMPUTE BOUNDARIES ===
    // i=1 and i=Nloc_r, j from 1..Nloc_c
    for(int i=1;i<=Nloc_r;i++){
        for(int j=1;j<=Nloc_c;j++){
            if(!(i>=2 && i<=Nloc_r-1 && j>=2 && j<=Nloc_c-1)) // boundary only
            {
                int idx = i*stride + j;
                int Gx = -tile[idx-stride-1] - 2*tile[idx-1] - tile[idx+stride-1]
                         + tile[idx-stride+1] + 2*tile[idx+1] + tile[idx+stride+1];

                int Gy = -tile[idx-stride-1] - 2*tile[idx-stride] - tile[idx-stride+1]
                         + tile[idx+stride-1] + 2*tile[idx+stride] + tile[idx+stride+1];

                out[(i-1)*Nloc_c + (j-1)] = clamp255(sqrt((double)Gx*Gx + Gy*Gy));
            }
        }
    }

    // === GATHER BACK ===
    vector<int> final_img;
    if(rank==0) final_img.assign(N*N,0);

    MPI_Gatherv(out.data(), Nloc_r*Nloc_c, MPI_INT,
                rank==0?final_img.data():NULL,
                sendcounts.data(), displs.data(), block,
                0, MPI_COMM_WORLD);

    if(rank==0){
        cout<<"MPI 2D Sobel Completed Successfully: N="<<N
            <<" using "<<Pr<<"x"<<Pc<<" grid.\n";
    }

    MPI_Type_free(&block);
    MPI_Finalize();
    return 0;
}
