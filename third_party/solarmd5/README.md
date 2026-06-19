# Solar Designer MD5

This directory contains the OpenSSL-compatible public-domain MD5 implementation
by Alexander Peslyak, also known as Solar Designer.

Upstream homepage:
http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5

GitHub mirror:
https://github.com/openwall/popa3d/tree/main/md5

The vendored `md5.c` file matches the GitHub mirror byte for byte. The
`md5.h` header is patched to defer to OpenSSL when `<openssl/md5.h>` has
already been included by a downstream translation unit.

The implementation is vendored here because it is external third-party code.
