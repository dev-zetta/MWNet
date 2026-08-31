#!/bin/sh -ex
echo -n | openssl s_client -connect scan.coverity.com:443 | sed -ne '/-BEGIN CERTIFICATE-/,/-END CERTIFICATE-/p' | sudo tee -a /etc/ssl/certs/ca-

# Set up compilers
if [ ! -z "${MATRIX_CC}" ]; then
    eval "${MATRIX_CC}"
fi
