#include "utils.h"
#include "worker.h"
#include <mpi.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>

static int g_session_key = 0;

// “bezbedno” primanje zadatka: XOR sa g_session_key
static void secure_recv_task(int *x, MPI_Comm comm)
{
    int enc = 0;
    MPI_Recv(&enc, 1, MPI_INT, 0, TAG_TASK, comm, MPI_STATUS_IGNORE);
    *x = enc ^ g_session_key;
}

void worker_TLS(const char *port, const int wr)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    srand((unsigned)time(NULL));

    // 1) Prvo spajanje: connect -> fake TLS -> merge(high=1)
    MPI_Comm inter;
    int rc = MPI_Comm_connect(port, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &inter);
    perr("Comm_connect#first", rc);

    // === FAKE "TLS" HANDSHAKE: WORKER STRANA ===
    AuthClientHello ch;
    AuthServerHello sh;
    AuthProof p;

    ch.worker_id = wr;
    ch.nonce_client = rand();

    // 1) Pošalji ClientHello masteru
    rc = MPI_Send(&ch, sizeof(ch), MPI_BYTE, 0, TAG_AUTH_CLIENT_HELLO, inter);
    perr("Send(AUTH_CLIENT_HELLO)", rc);

    // 2) Primi ServerHello sa server nonce-om
    rc = MPI_Recv(&sh, sizeof(sh), MPI_BYTE, 0, TAG_AUTH_SERVER_HELLO, inter, MPI_STATUS_IGNORE);
    perr("Recv(AUTH_SERVER_HELLO)", rc);

    // 3) Izračunaj "proof" (fake, XOR sa tajnom)
    int SECRET = 0x12345678; // isti kao u master.c

    p.worker_id = ch.worker_id;
    p.nonce_client = ch.nonce_client;
    p.nonce_server = sh.nonce_server;
    p.proof = ch.nonce_client ^ sh.nonce_server ^ SECRET;

    // 4) Pošalji proof masteru
    rc = MPI_Send(&p, sizeof(p), MPI_BYTE, 0, TAG_AUTH_PROOF, inter);
    perr("Send(AUTH_PROOF)", rc);

    // 5) Primi rezultat (da li je auth prošao)
    int accept_it = 0;
    rc = MPI_Recv(&accept_it, 1, MPI_INT, 0, TAG_AUTH_RESULT, inter, MPI_STATUS_IGNORE);
    perr("Recv(AUTH_RESULT)", rc);

    if (!accept_it)
    {
        printf("[WORKER %d] AUTH FAILED, aborting.\n", wr);
        fflush(stdout);
        rc = MPI_Comm_disconnect(&inter);
        perr("Disconnect(rejected)", rc);
        // MPI_Finalize();
        return;
    }

    // 6) Izračunaj i zapamti session key
    int K_session = ch.nonce_client ^ sh.nonce_server ^ SECRET;
    g_session_key = K_session;
    printf("[WORKER %d] session key = 0x%x\n", wr, K_session);
    fflush(stdout);

    // 1b) Merge u CLUSTER
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
        printf("pocetak FORA\n");
        fflush(stdout);

        // primi najavu more,len,port
        int more = 0, len = 0;
        char port[MPI_MAX_PORT_NAME];
        MPI_Bcast(&more, 1, MPI_INT, 0, CLUSTER);
        MPI_Bcast(&len, 1, MPI_INT, 0, CLUSTER);
        if (more && len > 0 && len <= MPI_MAX_PORT_NAME)
            MPI_Bcast(port, len, MPI_CHAR, 0, CLUSTER);

        printf("zavrsio bcast\n");
        fflush(stdout);

        if (!more)
        {
            // kraj admission-a
            break;
        }

        // svi “stari” članovi CLUSTER-a ulaze u kolektivni accept
        MPI_Comm inter2;
        rc = MPI_Comm_accept(port, MPI_INFO_NULL, 0, CLUSTER, &inter2);
        perr("Comm_accept#wave", rc);

        {
            int accept_it_wave = 0;
            // SVI stari članovi CLUSTER-a moraju da pročitaju odluku koja je bcast-ovana sa mastera
            MPI_Bcast(&accept_it_wave, 1, MPI_INT, 0, CLUSTER);
            printf("Prosao bcast\n");
            fflush(stdout);

            if (!accept_it_wave)
            {
                // AUTH odbijen → na istom inter2 svi rade DISCONNECT, bez merge-a
                MPI_Comm_disconnect(&inter2);

                // kraj runde (poravnanje)
                MPI_Barrier(CLUSTER);
                continue; // idemo u sledeću rundu
            }
        }

        // NEMA barijere na inter2 — merge je kolektivan
        MPI_Comm CL_NEW;
        printf("OVDE SAM\n");
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
                // umesto običnog MPI_Recv:
                secure_recv_task(&x, CLUSTER);

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
