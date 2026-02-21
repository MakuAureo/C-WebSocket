#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//Encodes data using base64 and returns a pointer to the encoded string on the heap (needs to be manually free'd)
unsigned char * base64_encode(const unsigned char *data, size_t input_length, size_t *output_length);

#endif
