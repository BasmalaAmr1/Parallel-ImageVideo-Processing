// mpi_pingpong.cpp
#include <mpi.h>
#include <iostream>
#include <vector>
#include <chrono>
using namespace std;
int main(int argc,char**argv){
    MPI_Init(&argc,&argv);
    int rank; MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    int partner = (rank==0?1:0);
    if (argc<2){ if(rank==0) cout<<"Usage: mpirun -np 2 ./mpi_pingpong MSG_SIZE_BYTES\n"; MPI_Finalize(); return 0;}
    int msgsize = stoi(argv[1]);
    vector<char> buf(msgsize);
    int Niter = 1000;
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank==0){
        auto t0 = chrono::high_resolution_clock::now();
        for (int i=0;i<Niter;++i){
            MPI_Send(buf.data(), msgsize, MPI_CHAR, partner, 0, MPI_COMM_WORLD);
            MPI_Recv(buf.data(), msgsize, MPI_CHAR, partner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        auto t1 = chrono::high_resolution_clock::now();
        double ms = chrono::duration_cast<chrono::duration<double, std::milli>>(t1-t0).count();
        double rtt = ms / Niter;
        double one_way = rtt/2.0;
        double bandwidth = (double)msgsize / (one_way/1000.0); // bytes/sec
        cout<<"MSG="<<msgsize<<" RTT_ms="<<rtt<<" one_way_ms="<<one_way<<" BW_bytes_per_s="<<bandwidth<<"\n";
    } else {
        for (int i=0;i<Niter;++i){
            MPI_Recv(buf.data(), msgsize, MPI_CHAR, partner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send(buf.data(), msgsize, MPI_CHAR, partner, 0, MPI_COMM_WORLD);
        }
    }
    MPI_Finalize();
    return 0;
}



// mpi_pingpong 8       # small msg (latency)
//mpi_pingpong 1000000    # large msg (bandwidth)