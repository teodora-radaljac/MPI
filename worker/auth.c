#include "utils.h"
#include "worker.h"
#include <mpi.h>
#include <stdio.h>

void worker_auth(const char *port)
{
    // (opciono) odmah viđi logove
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // 1) Prvo spajanje: connect -> merge(high=1)
    MPI_Comm inter;
    int rc = MPI_Comm_connect(port, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &inter);

    perr("Comm_connect#first", rc);

    // Slanje autentikacije
    int magic = 5;
    rc = MPI_Send(&magic, 1, MPI_INT, 0, TAG_AUTH, inter);
    perr("Send(AUTH)", rc);
    int accept_it = 0;
    rc = MPI_Recv(&accept_it, 1, MPI_INT, 0, TAG_AUTH_REPLY, inter, MPI_STATUS_IGNORE);
    perr("Recv(AUTH_REPLY)", rc);
    if (!accept_it)
    {
        // Master nas je odbio — uredno zatvori i izađi.
        rc = MPI_Comm_disconnect(&inter);
        perr("Disconnect(rejected)", rc);
        // MPI_Finalize();
        return;
    }

    MPI_Comm CLUSTER;
    rc = MPI_Intercomm_merge(inter, /*high=*/1, &CLUSTER);
    perr("Intercomm_merge#first", rc);
    MPI_Comm_disconnect(&inter);

    int rank, size;
    MPI_Comm_rank(CLUSTER, &rank);
    MPI_Comm_size(CLUSTER, &size);
    printf("[WORKER %d] joined CLUSTER: size=%d\n", rank, size);
    MPI_Barrier(CLUSTER);
    // 2) Admission runde
    for (;;)
    {
        printf("pocetak FORA");
        fflush(stdout);
        // poravnaj rundu sa trenutnim CLUSTER-om

        // primi najavu more,len,port
        int more = 0, len = 0;
        char port[MPI_MAX_PORT_NAME];
        MPI_Bcast(&more, 1, MPI_INT, 0, CLUSTER);
        MPI_Bcast(&len, 1, MPI_INT, 0, CLUSTER);
        if (more && len > 0 && len <= MPI_MAX_PORT_NAME)
            MPI_Bcast(port, len, MPI_CHAR, 0, CLUSTER);
        printf("zavrsio bcast");
        fflush(stdout);
        if (more)
        {
            //   printf("USAAOOOOO");
            // fflush(stdout);
        }
        else
        {
            // printf("BREAAAAAAK");
            // fflush(stdout);
            break; // kraj admission-a
        }
        // svi “stari” članovi CLUSTER-a ulaze u kolektivni accept
        MPI_Comm inter2;
        rc = MPI_Comm_accept(port, MPI_INFO_NULL, 0, CLUSTER, &inter2);
        perr("Comm_accept#wave", rc);
        {
            int accept_it = 0;
            // SVI stari članovi CLUSTER-a moraju da pročitaju odluku koja je bcast-ovana sa mastera
            MPI_Bcast(&accept_it, 1, MPI_INT, 0, CLUSTER);
            printf("Prosao bcast");
            fflush(stdout);
            if (!accept_it)
            {
                // AUTH odbijen → na istom inter2 svi rade DISCONNECT, bez merge-a
                MPI_Comm_disconnect(&inter2);

                // kraj runde (poravnanje)
                MPI_Barrier(CLUSTER);
                continue; // idemo u sledeću rundu
            }
        }
        // NEMA barijere na interu — merge je kolektivan
        MPI_Comm CL_NEW;
        printf("OVDE SAM");
        fflush(stdout);
        rc = MPI_Intercomm_merge(inter2, /*high (old side)*/ 0, &CL_NEW);
        perr("Intercomm_merge#wave", rc);
        MPI_Comm_disconnect(&inter2);

        MPI_Comm_free(&CLUSTER);
        CLUSTER = CL_NEW;

        MPI_Comm_rank(CLUSTER, &rank);
        MPI_Comm_size(CLUSTER, &size);
        printf("[WORKER %d] post-merge CLUSTER size=%d\n", rank, size);
        // poravnanje kraja runde
        MPI_Barrier(CLUSTER);
    }

    // 3) Task-farm: svi osim ranga 0 rade
    MPI_Comm_rank(CLUSTER, &rank);
    MPI_Comm_size(CLUSTER, &size);

    if (rank != 0)
    {
        for (;;)
        {
            MPI_Status st;
            MPI_Probe(0, MPI_ANY_TAG, CLUSTER, &st);

            if (st.MPI_TAG == TAG_IDLE)
            {
                MPI_Recv(NULL, 0, MPI_INT, 0, TAG_IDLE, CLUSTER, MPI_STATUS_IGNORE);
                continue;
            }
            if (st.MPI_TAG == TAG_TASK)
            {
                int x = 0;
                MPI_Recv(&x, 1, MPI_INT, 0, TAG_TASK, CLUSTER, MPI_STATUS_IGNORE);
                int y = x * x;
                int pair[2] = {x, y};
                MPI_Send(pair, 2, MPI_INT, 0, TAG_RESULT, CLUSTER);
                continue;
            }
            // fallback: progutaj neočekivane tagove
            MPI_Recv(NULL, 0, MPI_INT, 0, st.MPI_TAG, CLUSTER, MPI_STATUS_IGNORE);
        }
    }

    MPI_Comm_free(&CLUSTER);
}
