#ifndef ELARA_CODE_TEMPLATE_H
#define ELARA_CODE_TEMPLATE_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/StringList.h>
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

namespace elara {

class CodeTemplateBlock {
private:
    String name;
    String code;
    StringList attributes;

public:
    CodeTemplateBlock();
    CodeTemplateBlock(const String& block_name, const String& block_code, const StringList& block_attributes);

    String getName() const;
    String getCode() const;
    StringList getAttributes() const;
    String getAttribute(int index) const;
    bool hasAttribute(const String& attr) const;
};

class CodeTemplate {
private:
    Array< Ref<CodeTemplateBlock> > blocks;

    int findLineEnd(String data, int offset) const;
    int findLineStartMarker(String data, const String& marker, int offset) const;
    bool isBrokenTemplateCode(String code) const;
    StringList parseAttributes(String attr_data) const;
    void parseOpenLine(String line, String* name, StringList* attrs) const;

public:
    CodeTemplate();

    bool loadFile(const String& path);
    bool loadData(const String& data);

    int blockCount() const;
    Ref<CodeTemplateBlock> getBlock(int index) const;
    Ref<CodeTemplateBlock> getBlock(const String& name) const;

    String getCode(const String& name, StringList attribute_values) const;
    String getCode(const String& name) const;
    StringList getAttributes(const String& name) const;
    String getAttribute(const String& name, int index) const;

    bool hasBlock(const String& name) const;
    bool hasAttribute(const String& name, const String& attr) const;
};

}

#endif
