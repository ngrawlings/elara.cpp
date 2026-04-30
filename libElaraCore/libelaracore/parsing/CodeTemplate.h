#ifndef ELARA_CODE_TEMPLATE_H
#define ELARA_CODE_TEMPLATE_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

namespace elara {

class CodeTemplateBlock {
private:
    String name;
    String code;
    Array<String> attributes;

public:
    CodeTemplateBlock();
    CodeTemplateBlock(const String& block_name, const String& block_code, const Array<String>& block_attributes);

    String getName() const;
    String getCode() const;
    Array<String> getAttributes() const;
    String getAttribute(int index) const;
    bool hasAttribute(const String& attr) const;
};

class CodeTemplate {
private:
    Array< Ref<CodeTemplateBlock> > blocks;

    bool startsWithAt(const String& data, int offset, const String& marker) const;
    String readLine(const String& data, int* offset) const;
    Array<String> parseAttributes(String attr_data) const;
    void parseOpenLine(const String& line, String* name, Array<String>* attrs) const;

public:
    CodeTemplate();

    bool loadFile(const String& path);
    bool loadData(const String& data);

    int blockCount() const;
    Ref<CodeTemplateBlock> getBlock(int index) const;
    Ref<CodeTemplateBlock> getBlock(const String& name) const;

    String getCode(const String& name) const;
    Array<String> getAttributes(const String& name) const;
    String getAttribute(const String& name, int index) const;

    bool hasBlock(const String& name) const;
    bool hasAttribute(const String& name, const String& attr) const;
};

}

#endif
