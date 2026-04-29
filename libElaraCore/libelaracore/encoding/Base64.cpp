//
//  Base64.cpp
//  libelaracore
//
//  Created by Nyhl Rawlings on 29/04/2026.
//

#include "Base64.h"

namespace elara {

    namespace {

        static const char base64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        int base64Value(char c) {
            if (c >= 'A' && c <= 'Z')
                return c - 'A';
            if (c >= 'a' && c <= 'z')
                return c - 'a' + 26;
            if (c >= '0' && c <= '9')
                return c - '0' + 52;
            if (c == '+')
                return 62;
            if (c == '/')
                return 63;
            return -1;
        }

    }

    Memory Base64::encode(Memory mem) {
        if (!mem.length())
            return Memory(0);

        size_t output_length = ((mem.length() + 2) / 3) * 4;
        Memory ret(output_length + 1);
        const unsigned char *input = (const unsigned char *)mem.getPtr();
        char *output = ret.getPtr();
        size_t input_index = 0;
        size_t output_index = 0;

        while (input_index < mem.length()) {
            unsigned int octet_a = input_index < mem.length() ? input[input_index++] : 0;
            unsigned int octet_b = input_index < mem.length() ? input[input_index++] : 0;
            unsigned int octet_c = input_index < mem.length() ? input[input_index++] : 0;
            unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

            output[output_index++] = base64_alphabet[(triple >> 18) & 0x3F];
            output[output_index++] = base64_alphabet[(triple >> 12) & 0x3F];
            output[output_index++] = base64_alphabet[(triple >> 6) & 0x3F];
            output[output_index++] = base64_alphabet[triple & 0x3F];
        }

        int remainder = (int)(mem.length() % 3);
        if (remainder) {
            output[output_length - 1] = '=';
            if (remainder == 1)
                output[output_length - 2] = '=';
        }

        output[output_length] = 0;
        ret.crop(output_length + 1);
        return ret;
    }

    Memory Base64::decode(Memory mem) {
        if (!mem.length())
            return Memory(0);

        size_t input_length = mem.length();
        const char *input = mem.getPtr();

        while (input_length && input[input_length - 1] == 0)
            input_length--;

        if (!input_length || (input_length % 4))
            return Memory(0);

        size_t output_length = (input_length / 4) * 3;
        if (input[input_length - 1] == '=')
            output_length--;
        if (input_length > 1 && input[input_length - 2] == '=')
            output_length--;

        Memory ret(output_length);
        unsigned char *output = (unsigned char *)ret.getPtr();
        size_t input_index = 0;
        size_t output_index = 0;

        while (input_index < input_length) {
            char c0 = input[input_index++];
            char c1 = input[input_index++];
            char c2 = input[input_index++];
            char c3 = input[input_index++];

            int sextet_a = base64Value(c0);
            int sextet_b = base64Value(c1);
            int sextet_c = (c2 == '=') ? 0 : base64Value(c2);
            int sextet_d = (c3 == '=') ? 0 : base64Value(c3);

            if (sextet_a < 0 || sextet_b < 0 || (c2 != '=' && sextet_c < 0) || (c3 != '=' && sextet_d < 0))
                return Memory(0);

            unsigned int triple = (sextet_a << 18) | (sextet_b << 12) | (sextet_c << 6) | sextet_d;

            if (output_index < output_length)
                output[output_index++] = (triple >> 16) & 0xFF;
            if (output_index < output_length)
                output[output_index++] = (triple >> 8) & 0xFF;
            if (output_index < output_length)
                output[output_index++] = triple & 0xFF;
        }

        return ret;
    }

}
