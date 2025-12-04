#ifndef MPI_UTILS_H
#define MPI_UTILS_H

#include <string.h>

#define PORT_FILE_NAME "port.txt"
#define MAX_PROCS 128

#define REPORT_SIZE 0x4A0 // 1184 bytes
#define REPORT_DATA 64
#define SKIP 0x20 
#define SEV_GUEST_DEV "/dev/sev-guest"

#define AUTH "auth"
#define NO_TLS "noTLS"
#define TLS "TLS"

typedef enum {
    MODE_AUTH,
    MODE_NOTLS,
    MODE_TLS,
    MODE_INVALID
} mode_comm;

enum
{
  TAG_HELLO = 1,
  TAG_MERGE_CMD = 2,
  TAG_READY = 3,
  TAG_TASK = 10,
  TAG_RESULT = 11,
  TAG_IDLE = 13,
  TAG_AUTH = 88,
  TAG_AUTH_REPLY = 89,
  TAG_AUTH_CLIENT_HELLO = 90,
  TAG_AUTH_SERVER_HELLO = 91,
  TAG_AUTH_PROOF = 92,
  TAG_AUTH_RESULT = 93
};

typedef struct
{
  int worker_id;
  int nonce_client;
} AuthClientHello;

typedef struct
{
  int nonce_server;
  char server_data[REPORT_DATA];
} AuthServerHello;

typedef struct
{
  int worker_id;
  int nonce_client;
  int nonce_server;
  char server_data[REPORT_DATA];
  char report[REPORT_SIZE];
  int proof;
} AuthProof;

mode_comm parse_mode(const char *arg);

#endif // MPI_UTILS_H