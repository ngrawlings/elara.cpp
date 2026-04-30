#include "CodeTemplate.h"

#include <libelaracore/memory/Memory.h>
#include <libelaraio/File.h>

namespace elara {

CodeTemplateBlock::CodeTemplateBlock() {}

CodeTemplateBlock::CodeTemplateBlock(
    const String& block_name,
    const String& block_code,
    const Array<String>& block_attributes
) : name(block_name),
    code(block_code),
    attributes(block_attributes) {}

String CodeTemplateBlock::getName() const {
    return name;
}

String CodeTemplateBlock::getCode() const {
    return code;
}

Array<String> CodeTemplateBlock::getAttributes() const {
    return attributes;
}

String CodeTemplateBlock::getAttribute(int index) const {
    if(index < 0 || index >= (int)attributes.length()) {
        return String("");
    }

    return attributes[index];
}

bool CodeTemplateBlock::hasAttribute(const String& attr) const {
    for(int i = 0; i < (int)attributes.length(); i++) {
        if(attributes[i] == attr) {
            return true;
        }
    }

    return false;
}

CodeTemplate::CodeTemplate() {}

bool CodeTemplate::startsWithAt(const String& data, int offset, const String& marker) const {
    if(offset < 0 || offset + marker.length() > data.length()) {
        return false;
    }

    for(int i = 0; i < marker.length(); i++) {
        if(data[offset + i] != marker[i]) {
            return false;
        }
    }

    return true;
}

String CodeTemplate::readLine(const String& data, int* offset) const {
    int start = *offset;

    while(*offset < data.length()) {
        char c = data.byteAt(*offset);
        (*offset)++;

        if(c == '\n') {
            int len = (*offset) - start - 1;

            if(len > 0 && data[start + len - 1] == '\r') {
                len--;
            }

            return data.substr(start, len);
        }
    }

    return data.substr(start, (*offset) - start);
}

Array<String> CodeTemplate::parseAttributes(String attr_data) const {
    Array<String> attrs;
    int offset = 0;

    while(offset <= attr_data.length()) {
        int next = attr_data.indexOf(String(">"), offset);
        String attr;

        if(next < 0) {
            attr = attr_data.substr(offset).trim();
            offset = attr_data.length() + 1;
        } else {
            attr = attr_data.substr(offset, next - offset).trim();
            offset = next + 1;
        }

        if(attr.length() > 0) {
            attrs.push(attr);
        }
    }

    return attrs;
}

void CodeTemplate::parseOpenLine(const String& line, String* name, Array<String>* attrs) const {
    String tail = line.substr(10).trim();
    int attr_start = tail.indexOf(String(">>>>"));

    if(attr_start < 0) {
        *name = tail.trim();
        attrs->clear();
        return;
    }

    *name = tail.substr(0, attr_start).trim();

    String attr_data = tail.substr(attr_start + 4).trim();
    *attrs = parseAttributes(attr_data);
}

bool CodeTemplate::loadFile(const String& path) {
    File file((const char*)path);
    String data = (Memory)file;
    return loadData(data);
}

bool CodeTemplate::loadData(const String& data) {
    blocks.clear();

    int offset = 0;
    bool in_block = false;

    String current_name;
    String current_code;
    Array<String> current_attrs;

    while(offset < data.length()) {
        String line = readLine(data, &offset);

        if(!in_block && line.startsWith(String(">>>>>>>>>>"))) {
            parseOpenLine(line, &current_name, &current_attrs);
            current_code = String("");
            in_block = true;
            continue;
        }

        if(in_block && line.startsWith(String("<<<<<<<<<<"))) {
            String close_name = line.substr(10).trim();

            if(close_name == current_name) {
                Ref<CodeTemplateBlock> block(
                    new CodeTemplateBlock(current_name, current_code, current_attrs)
                );

                blocks.push(block);

                current_name = String("");
                current_code = String("");
                current_attrs.clear();
                in_block = false;
                continue;
            }
        }

        if(in_block) {
            current_code += line;
            current_code += String("\n");
        }
    }

    return true;
}

int CodeTemplate::blockCount() const {
    return (int)blocks.length();
}

Ref<CodeTemplateBlock> CodeTemplate::getBlock(int index) const {
    if(index < 0 || index >= (int)blocks.length()) {
        return Ref<CodeTemplateBlock>(0);
    }

    return blocks[index];
}

Ref<CodeTemplateBlock> CodeTemplate::getBlock(const String& name) const {
    for(int i = 0; i < (int)blocks.length(); i++) {
        if(blocks[i]->getName() == name) {
            return blocks[i];
        }
    }

    return Ref<CodeTemplateBlock>(0);
}

String CodeTemplate::getCode(const String& name) const {
    Ref<CodeTemplateBlock> block = getBlock(name);

    if(!block) {
        return String("");
    }

    return block->getCode();
}

Array<String> CodeTemplate::getAttributes(const String& name) const {
    Ref<CodeTemplateBlock> block = getBlock(name);

    if(!block) {
        return Array<String>();
    }

    return block->getAttributes();
}

String CodeTemplate::getAttribute(const String& name, int index) const {
    Ref<CodeTemplateBlock> block = getBlock(name);

    if(!block) {
        return String("");
    }

    return block->getAttribute(index);
}

bool CodeTemplate::hasBlock(const String& name) const {
    return (bool)getBlock(name);
}

bool CodeTemplate::hasAttribute(const String& name, const String& attr) const {
    Ref<CodeTemplateBlock> block = getBlock(name);

    if(!block) {
        return false;
    }

    return block->hasAttribute(attr);
}

}
