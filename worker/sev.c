#include <fcntl.h>
#include <linux/sev-guest.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include "utils.h"

int fetch_attestation_report(const char *data, char *out_report) {
    struct snp_report_resp resp = {0};
    struct snp_report_req req = {0};
    struct snp_guest_request_ioctl io = {0};
    int fd = open(SEV_GUEST_DEV, O_RDWR);

    if (fd < 0) {
        perror("open(/dev/sev-guest)");
        return -1;
    }

    for (int i = 0; i < 64; i++) 
        req.user_data[i] = (unsigned char)data[i];
    req.vmpl = 0;
    
    io.msg_version = 1; // must be non-zero
    io.req_data    = (uintptr_t)&req;
    io.resp_data   = (uintptr_t)&resp;

    if (ioctl(fd, SNP_GET_REPORT, &io) != 0) {
        int e = errno;
        fprintf(stderr, "SNP_GET_REPORT failed: errno=%d (%s), fw_error=%u vmm_error=%u\n",
                e, strerror(e), io.fw_error, io.vmm_error);
        close(fd);
        return -1;
    }

    for(int i = SKIP; i < SKIP + REPORT_SIZE; i++) {
        out_report[i - SKIP] = resp.data[i];
    }

    return 0;
}