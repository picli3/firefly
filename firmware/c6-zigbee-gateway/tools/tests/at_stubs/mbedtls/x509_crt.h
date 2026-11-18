#pragma once
#include <stddef.h>
#include <openssl/x509.h>
typedef struct { X509 *crt; } mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *);
void mbedtls_x509_crt_free(mbedtls_x509_crt *);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *, const unsigned char *, size_t);
