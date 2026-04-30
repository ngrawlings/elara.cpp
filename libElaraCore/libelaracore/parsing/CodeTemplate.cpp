#include "CodeTemplate.h"

#include <libelaracore/memory/Memory.h>
#include <libelaracore/parsing/RegularExpression.h>
#include <libelaraio/File.h>

namespace elara {

CodeTemplateBlock::CodeTemplateBlock() {}

CodeTemplateBlock::CodeTemplateBlock(
    const String& block_name,
    const String& block_code,
    const StringList& block_attributes
) : name(block_name),
    code(block_code),
    attributes(block_attributes) {}

String CodeTemplateBlock::getName() const {
    return name;
}

String CodeTemplateBlock::getCode() const {
    return code;
}

StringList CodeTemplateBlock::getAttributes() const {
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

int CodeTemplate::findLineEnd(String data, int offset) const {
    int line_end = data.indexOf(String("\n"), offset);

    if(line_end < 0) {
        return (int)data.length();
    }

    return line_end;
}

int CodeTemplate::findLineStartMarker(String data, const String& marker, int offset) const {
    int candidate = offset;

    while((candidate = data.indexOf(marker, candidate)) >= 0) {
        if(candidate == 0 || data.substr(candidate - 1, 1) == String("\n")) {
            return candidate;
        }

        candidate++;
    }

    return -1;
}

bool CodeTemplate::isBrokenTemplateCode(String code) const {
    RegularExpression open_marker(String(".*>>>>.*"));
    RegularExpression close_marker(String(".*<<<<.*"));

    return open_marker.match(code) || close_marker.match(code);
}

StringList CodeTemplate::parseAttributes(String attr_data) const {
    StringList attrs;
    int offset = 0;

    while(offset <= attr_data.length()) {
        int next = attr_data.indexOf(String(">"), offset);
        String attr;

        if(next < 0) {
            attr = attr_data.substr(offset).trim();
            offset = (int)attr_data.length() + 1;
        } else {
            attr = attr_data.substr(offset, next - offset).trim();
            offset = next + 1;
        }

        if(attr.length() > 0) {
            attrs.append(attr);
        }
    }

    return attrs;
}

void CodeTemplate::parseOpenLine(String line, String* name, StringList* attrs) const {
    RegularExpression open_line(String("^>>>>>>>>>>.*"));

    if(!open_line.match(line)) {
        throw "Template Error: invalid opening marker";
    }

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

    String source = data;
    int offset = 0;

    while(offset < source.length()) {
        int open_start = findLineStartMarker(source, String(">>>>>>>>>>"), offset);

        if(open_start < 0) {
            break;
        }

        int open_end = findLineEnd(source, open_start);
        String open_line = source.substr(open_start, open_end - open_start).trim();

        String current_name;
        StringList current_attrs;
        parseOpenLine(open_line, &current_name, &current_attrs);

        int code_start = open_end;
        if(code_start < source.length() && source.substr(code_start, 1) == String("\n")) {
            code_start++;
        }

        int close_start = findLineStartMarker(source, String("<<<<<<<<<<"), code_start);

        if(close_start < 0) {
            printf("Template Error: missing closing marker for %s\n", (const char*)current_name);
            throw "Template Error: missing closing marker";
        }

        int close_end = findLineEnd(source, close_start);
        String close_line = source.substr(close_start, close_end - close_start).trim();
        String close_name = close_line.substr(10).trim();

        if(!(close_name == current_name)) {
            printf(
                "Template Error: closing marker mismatch. opened '%s' but closed '%s'\n",
                (const char*)current_name,
                (const char*)close_name
            );
            throw "Template Error: closing marker mismatch";
        }

        String current_code = source.substr(code_start, close_start - code_start);

        if(isBrokenTemplateCode(current_code)) {
            printf("Template Error: broken nested marker inside %s\n", (const char*)current_name);
            printf("%s\n", (const char*)current_code);
            throw "Template Error: broken nested marker";
        }

        Ref<CodeTemplateBlock> block(
            new CodeTemplateBlock(current_name, current_code, current_attrs)
        );

        blocks.push(block);

        offset = close_end;
        if(offset < source.length() && source.substr(offset, 1) == String("\n")) {
            offset++;
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

String CodeTemplate::getCode(const String& name, StringList attribute_values) const {
    Ref<CodeTemplateBlock> block = getBlock(name);

    if(!block) {
        return String("");
    }

    String code = block->getCode();
    StringList attr = block->getAttributes();

    if (attr.length() > attribute_values.length()) {
        throw "Insufficient attributes provided";
    }

    for (int i=0; i<attr.length(); i++) {
        code = code.replace(String("%?%").arg(attr[i], "?"), attribute_values[i]);
    }

    code = processIncludes(code, attr, attribute_values);

    return code;
}

String CodeTemplate::getCode(const String& name) const {
    return this->getCode(name, StringList());
}

StringList CodeTemplate::getAttributes(const String& name) const {
    Ref<CodeTemplateBlock> block = getBlock(name);

    if(!block) {
        return StringList();
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

void CodeTemplate::setTemplateLoader(CodeTemplateLoader* loader) {
    this->loader = loader;
}

bool CodeTemplate::evalIncludeCondition(
    String condition,
    const StringList& attribute_names,
    const StringList& attribute_values
) const {
    condition = condition.trim();

    for(int i = 0; i < (int)attribute_names.length(); i++) {
        String marker = String("%?%").arg(attribute_names[i], "?");

        if(i < (int)attribute_values.length()) {
            condition = condition.replace(marker, attribute_values[i]);
        }
    }

    RegularExpression expr(
        String("^[[:space:]]*([^=!<>[:space:]]+)[[:space:]]*(==|!=|<=|>=|<|>)[[:space:]]*([^=!<>[:space:]]+)[[:space:]]*$")
    );

    StringList matches = expr.extract(condition);

    if(matches.length() < 4)
        return false;

    String left = matches[1].trim();
    String op = matches[2].trim();
    String right = matches[3].trim();

    double lnum = atof((const char*)left);
    double rnum = atof((const char*)right);

    if(op == String("=="))
        return left == right || lnum == rnum;

    if(op == String("!="))
        return !(left == right || lnum == rnum);

    if(op == String("<"))
        return lnum < rnum;

    if(op == String(">"))
        return lnum > rnum;

    if(op == String("<="))
        return lnum <= rnum;

    if(op == String(">="))
        return lnum >= rnum;

    return false;
}

String CodeTemplate::runInclude(
    const String& _template,
    const String& block,
    const StringList& include_params
) const {
    return this->loader->loadTemplate(_template, block, include_params);
}

String CodeTemplate::processIncludes(
    const String& code,
    const StringList& attribute_names,
    const StringList& attribute_values
) const {
    String result;
    String remaining = code;

    RegularExpression include_expr(
        String("@include[[:space:]]+\\[([^]]*)\\][[:space:]]+([A-Za-z0-9_.]+)>>>>([^\\n\\r]*)")
    );

    while(true) {
        StringList matches = include_expr.extract(remaining);

        if(matches.length() < 4) {
            result += remaining;
            break;
        }

        String full_match = matches[0];
        String condition = matches[1];
        String include_target = matches[2];
        String param_data = matches[3];

        int pos = remaining.indexOf(full_match);

        if(pos < 0) {
            result += remaining;
            break;
        }

        result += remaining.substr(0, pos);

        if(evalIncludeCondition(condition, attribute_names, attribute_values)) {
            StringList include_params(param_data, String(">"));

            int dot = include_target.indexOf(String("."));

            if(dot < 0) {
                throw "Invalid include target";
            }

            String _template = include_target.substr(0, dot).trim();
            String block = include_target.substr(dot + 1).trim();

            result += runInclude(_template, block, include_params);
        }

        remaining = remaining.substr(pos + full_match.length());
    }

    return result;
}

}
