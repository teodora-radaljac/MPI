#ifndef MPI_WORKER_H
#define MPI_WORKER_H

void perr(const char *where, int rc);

void worker_auth(const char *port);
void worker_noTLS(const char *port);
void worker_TLS(const char *port, const int wr);
int fetch_attestation_report(const char *data, char *out_report);

#endif // MPI_WORKER_H