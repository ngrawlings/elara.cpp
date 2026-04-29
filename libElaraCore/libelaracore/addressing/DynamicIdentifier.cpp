#include "DynamicIdentifier.h"

#include <libelaracore/memory/ByteArray.h>

namespace elara {

    DynamicIdentifier::DynamicIdentifier(const Memory &id) {
        if (!id.length())
            return;

        id_components.push((unsigned char)id.getByte(0));

        Array<int> parts;
        for (size_t i = 1; i < id.length(); i++) {
            int part = (unsigned char)id.getByte(i);
            parts.push(part);

            if (part < 0x80) {
                for (size_t x = 0; x + 1 < parts.length(); x++)
                    parts.get((unsigned int)x) -= 0x80;

                int value = 0;
                for (size_t x = 0; x < parts.length(); x++)
                    value += parts.get((unsigned int)(parts.length() - x - 1)) << (x * 7);

                id_components.push(value);
                parts.clear();
            }
        }
    }

    DynamicIdentifier::DynamicIdentifier(String id) {
        int value = 0;
        bool has_digit = false;

        for (ssize_t i = 0; i < id.length(); i++) {
            char c = id[i];

            if (c >= '0' && c <= '9') {
                value = (value * 10) + (c - '0');
                has_digit = true;
            } else if (c == '.') {
                if (has_digit) {
                    id_components.push(value);
                    value = 0;
                    has_digit = false;
                }
            }
        }

        if (has_digit)
            id_components.push(value);
    }

    DynamicIdentifier::~DynamicIdentifier() {
    }

    int DynamicIdentifier::componentCount() {
        return (int)id_components.length();
    }

    int DynamicIdentifier::get(int index) {
        return id_components.get(index);
    }

    Array<int> DynamicIdentifier::getIntArray() {
        return id_components;
    }

    Memory DynamicIdentifier::getBytes() {
        if (!id_components.length())
            return Memory();

        ByteArray bytes;
        char first = (char)id_components.get(0);
        bytes.append(&first, 1);

        for (size_t i = 1; i < id_components.length(); i++) {
            int value = id_components.get((unsigned int)i);
            Array<int> parts;

            while (value >= 0x80) {
                parts.push(value % 0x80);
                value >>= 7;
            }
            parts.push(value);

            for (size_t x = 0; x + 1 < parts.length(); x++) {
                char part = (char)(parts.get((unsigned int)(parts.length() - x - 1)) + 0x80);
                bytes.append(&part, 1);
            }

            char part = (char)parts.get(0);
            bytes.append(&part, 1);
        }

        return Memory(bytes);
    }

    String DynamicIdentifier::getDelimitedStrting() {
        String ret;

        for (size_t i = 0; i < id_components.length(); i++) {
            if (i)
                ret += ".";
            ret += String(id_components.get((unsigned int)i));
        }

        return ret;
    }

    void DynamicIdentifier::increment() {
        if (!id_components.length())
            return;
        id_components.get((unsigned int)(id_components.length() - 1))++;
    }

    void DynamicIdentifier::convertToChild(int child_node_id) {
        id_components.push(child_node_id);
    }

}
