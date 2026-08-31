// Build:
//   gcc -O2 -std=gnu11 -Wall -Wextra -o verify_snp_sig verify_snp_sig.c -lcrypto
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>

#define SNP_SIGNED_LEN          0x2A0   // bytes 0x000..0x29F inclusive :contentReference[oaicite:9]{index=9}
#define SNP_SIGNATURE_OFFSET    0x2A0   // signature starts here :contentReference[oaicite:10]{index=10}
#define SNP_SIGNATURE_TOTAL_LEN 0x200   // 0x2A0..0x49F :contentReference[oaicite:11]{index=11}

#define SNP_SIG_R_OFFSET        0x00
#define SNP_SIG_S_OFFSET        0x48
#define SNP_SIG_RS_LEN          72      // 576 bits = 72 bytes :contentReference[oaicite:12]{index=12}

#define SHA384_LEN              48

static int read_file(const char *path, uint8_t **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: fopen(%s): %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "ERROR: fseek(%s): %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "ERROR: ftell(%s): %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    rewind(f);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr, "ERROR: malloc(%ld) failed\n", sz);
        fclose(f);
        return -1;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (n != (size_t)sz) {
        fprintf(stderr, "ERROR: fread(%s): expected %ld got %zu\n", path, sz, n);
        free(buf);
        return -1;
    }

    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

static X509 *load_cert_pem(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: fopen(cert %s): %s\n", path, strerror(errno));
        return NULL;
    }
    X509 *cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);

    if (!cert) {
        fprintf(stderr, "ERROR: PEM_read_X509 failed (is it PEM?)\n");
        return NULL;
    }
    return cert;
}

static void reverse72(uint8_t out[SNP_SIG_RS_LEN], const uint8_t in[SNP_SIG_RS_LEN]) {
    for (size_t i = 0; i < SNP_SIG_RS_LEN; i++) {
        out[i] = in[SNP_SIG_RS_LEN - 1 - i];
    }
}

static int sha384_digest(const uint8_t *data, size_t len, uint8_t out[SHA384_LEN]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;

    unsigned int out_len = 0;
    int ok = EVP_DigestInit_ex(ctx, EVP_sha384(), NULL) &&
             EVP_DigestUpdate(ctx, data, len) &&
             EVP_DigestFinal_ex(ctx, out, &out_len);

    EVP_MD_CTX_free(ctx);

    if (!ok || out_len != SHA384_LEN) return -1;
    return 0;
}

static int verify_snp_report_sig(const uint8_t *report, size_t report_len, X509 *cert) {
    if (report_len < SNP_SIGNATURE_OFFSET + SNP_SIGNATURE_TOTAL_LEN) {
        fprintf(stderr, "ERROR: report too small (%zu bytes), need at least %u\n",
                report_len, (unsigned)(SNP_SIGNATURE_OFFSET + SNP_SIGNATURE_TOTAL_LEN));
        return -1;
    }

    // 1) Hash signed bytes (0x000..0x29F inclusive => 0x2A0 bytes) :contentReference[oaicite:13]{index=13}
    uint8_t digest[SHA384_LEN];
    if (sha384_digest(report, SNP_SIGNED_LEN, digest) != 0) {
        fprintf(stderr, "ERROR: SHA-384 digest failed\n");
        return -1;
    }

    // 2) Extract R and S (each 72 bytes little-endian) :contentReference[oaicite:14]{index=14}
    const uint8_t *sig_base = report + SNP_SIGNATURE_OFFSET;
    const uint8_t *r_le = sig_base + SNP_SIG_R_OFFSET;
    const uint8_t *s_le = sig_base + SNP_SIG_S_OFFSET;

    uint8_t r_be[SNP_SIG_RS_LEN], s_be[SNP_SIG_RS_LEN];
    reverse72(r_be, r_le);
    reverse72(s_be, s_le);

    BIGNUM *r = BN_bin2bn(r_be, SNP_SIG_RS_LEN, NULL);
    BIGNUM *s = BN_bin2bn(s_be, SNP_SIG_RS_LEN, NULL);
    if (!r || !s) {
        fprintf(stderr, "ERROR: BN_bin2bn failed\n");
        BN_free(r); BN_free(s);
        return -1;
    }

    ECDSA_SIG *sig = ECDSA_SIG_new();
    if (!sig) {
        fprintf(stderr, "ERROR: ECDSA_SIG_new failed\n");
        BN_free(r); BN_free(s);
        return -1;
    }

    // ECDSA_SIG takes ownership of r and s on success.
    if (ECDSA_SIG_set0(sig, r, s) != 1) {
        fprintf(stderr, "ERROR: ECDSA_SIG_set0 failed\n");
        ECDSA_SIG_free(sig);
        BN_free(r); BN_free(s);
        return -1;
    }

    // 3) Get EC public key from cert
    EVP_PKEY *pkey = X509_get_pubkey(cert);
    if (!pkey) {
        fprintf(stderr, "ERROR: X509_get_pubkey failed\n");
        ECDSA_SIG_free(sig);
        return -1;
    }

    EVP_PKEY *ec = EVP_PKEY_get1_EC_KEY(pkey);
    EVP_PKEY_free(pkey);

    if (!ec) {
        fprintf(stderr, "ERROR: cert public key is not EC\n");
        ECDSA_SIG_free(sig);
        return -1;
    }

    // 4) Verify signature over SHA-384 digest
    int v = ECDSA_do_verify(digest, SHA384_LEN, sig, ec);

    EC_KEY_free(ec);
    ECDSA_SIG_free(sig);

    if (v == 1) return 0;      // valid
    if (v == 0) return 1;      // invalid signature
    return -1;                 // error
}

int verify_attestation_report( const char *report_path, const char *cert_path) {
    uint8_t *report = NULL;
    size_t report_len = 0;
    if (read_file(report_path, &report, &report_len) != 0) {
        return 2;
    }

    X509 *cert = load_cert_pem(cert_path);
    if (!cert) {
        free(report);
        return 2;
    }

    int rc = verify_snp_report_sig(report, report_len, cert);

    if (rc == 0) {
        printf("SIGNATURE OK\n");
    } else if (rc == 1) {
        printf("SIGNATURE INVALID\n");
    } else {
        printf("SIGNATURE VERIFY ERROR\n");
    }

    X509_free(cert);
    free(report);
    return (rc == 0) ? 0 : 1;
}
