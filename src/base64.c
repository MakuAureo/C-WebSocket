#include "base64.h"

#include <stdint.h>
#include <stdlib.h>

void base64_encode(const unsigned char *data, size_t input_length, unsigned char ** output, size_t *output_length) {
  *output_length = 4 * ((input_length + 2) / 3);
  if (output == NULL) return;

  for (size_t i = 0, j = 0; i < input_length;) {
    uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
    uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
    uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    (*output)[j++] = base64_table[(triple >> 3 * 6) & 0x3F];
    (*output)[j++] = base64_table[(triple >> 2 * 6) & 0x3F];
    (*output)[j++] = base64_table[(triple >> 1 * 6) & 0x3F];
    (*output)[j++] = base64_table[(triple >> 0 * 6) & 0x3F];
  }

  for (size_t i = 0; i < (3 - input_length % 3) % 3; i++)
    (*output)[*output_length - 1 - i] = '=';

  (*output)[*output_length] = '\0';
}
