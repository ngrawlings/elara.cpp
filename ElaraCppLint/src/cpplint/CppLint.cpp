#include "CppLint.h"

#include <dirent.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>

namespace elara {

    CppLint::CppLint() {
    }

    int CppLint::run(int argc, const char* const* argv) {
        if (!parseArguments(argc, argv))
            return 1;

        if (!collectFiles())
            return 1;

        for (size_t i=0; i<files.size(); i++) {
            lintFile(files[i]);
        }

        for (size_t i=0; i<violations.size(); i++) {
            Violation& violation = violations[i];
            fprintf(stderr, "%s:%d: %s\n", violation.path.operator char *(), violation.line, violation.message.operator char *());
        }

        if (!violations.empty()) {
            fprintf(stderr, "elara.cpp-lint: %lu violation(s) found across %lu file(s)\n",
                    (unsigned long)violations.size(),
                    (unsigned long)files.size());
            return 1;
        }

        fprintf(stdout, "elara.cpp-lint: clean (%lu file(s) checked)\n", (unsigned long)files.size());
        return 0;
    }

    bool CppLint::parseArguments(int argc, const char* const* argv) {
        if (argc < 2) {
            printUsage(argv[0]);
            return false;
        }

        for (int i=1; i<argc; i++) {
            String arg(argv[i]);
            if (arg == String("--help") || arg == String("-h")) {
                printUsage(argv[0]);
                return false;
            }
            input_paths.push_back(arg);
        }

        return !input_paths.empty();
    }

    void CppLint::printUsage(const char* argv0) const {
        fprintf(stderr, "Usage: %s <file-or-directory> [more paths...]\n", argv0);
        fprintf(stderr, "Flags non-primitive declarations unless they use Ref, RefArray, or elara::threading::memory::Ref.\n");
    }

    bool CppLint::collectFiles() {
        bool ok = true;
        for (size_t i=0; i<input_paths.size(); i++) {
            if (!collectPath(input_paths[i]))
                ok = false;
        }
        return ok;
    }

    bool CppLint::collectPath(const String& path) {
        String path_copy(path);
        struct stat st;
        if (stat(path_copy.operator char *(), &st) != 0) {
            fprintf(stderr, "elara.cpp-lint: failed to stat %s\n", path_copy.operator char *());
            return false;
        }

        if (S_ISDIR(st.st_mode)) {
            if (shouldSkipDirectory(path))
                return true;

            DIR* dir = opendir(path_copy.operator char *());
            if (!dir) {
                fprintf(stderr, "elara.cpp-lint: failed to open directory %s\n", path_copy.operator char *());
                return false;
            }

            struct dirent* ent;
            while ((ent = readdir(dir)) != 0) {
                if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                    continue;

                String child = path + String("/") + String(ent->d_name);
                if (!collectPath(child)) {
                    closedir(dir);
                    return false;
                }
            }

            closedir(dir);
            return true;
        }

        if (S_ISREG(st.st_mode) && isCppPath(path)) {
            files.push_back(path);
        }

        return true;
    }

    bool CppLint::lintFile(const String& path) {
        bool ok = false;
        String source = readFile(path, &ok);
        if (!ok) {
            String path_copy(path);
            fprintf(stderr, "elara.cpp-lint: failed to read %s\n", path_copy.operator char *());
            return false;
        }

        String sanitized = sanitizeSource(source);
        TokenList tokens = tokenize(sanitized);
        lintTokens(path, tokens);
        return true;
    }

    bool CppLint::isCppPath(String path) const {
        return path.endsWith(".cpp") ||
               path.endsWith(".cc") ||
               path.endsWith(".cxx") ||
               path.endsWith(".h") ||
               path.endsWith(".hh") ||
               path.endsWith(".hpp") ||
               path.endsWith(".ipp");
    }

    bool CppLint::shouldSkipDirectory(String path) const {
        return path.endsWith("/build") ||
               path.endsWith("/.git") ||
               path.endsWith("/autom4te.cache");
    }

    String CppLint::readFile(String path, bool* ok) const {
        *ok = false;

        FILE* fp = fopen(path.operator char *(), "rb");
        if (!fp)
            return String();

        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            return String();
        }

        long len = ftell(fp);
        if (len < 0) {
            fclose(fp);
            return String();
        }

        rewind(fp);

        char* buffer = new char[(size_t)len + 1];
        size_t read_len = fread(buffer, 1, (size_t)len, fp);
        buffer[read_len] = 0;
        fclose(fp);

        String ret(buffer, read_len);
        delete [] buffer;
        *ok = true;
        return ret;
    }

    String CppLint::sanitizeSource(String source) const {
        size_t len = (size_t)source.length();
        char* clean = new char[len + 1];
        const char* data = source.operator char *();

        enum State {
            Normal,
            LineComment,
            BlockComment,
            StringLiteral,
            CharLiteral,
            Preprocessor
        } state = Normal;

        bool line_start = true;

        for (size_t i=0; i<len; i++) {
            char c = data[i];
            char next = (i + 1 < len) ? data[i + 1] : 0;

            if (state == Normal) {
                if (line_start && c == '#') {
                    state = Preprocessor;
                    clean[i] = ' ';
                } else if (c == '/' && next == '/') {
                    state = LineComment;
                    clean[i] = ' ';
                } else if (c == '/' && next == '*') {
                    state = BlockComment;
                    clean[i] = ' ';
                } else if (c == '"') {
                    state = StringLiteral;
                    clean[i] = ' ';
                } else if (c == '\'') {
                    state = CharLiteral;
                    clean[i] = ' ';
                } else {
                    clean[i] = c;
                }
            } else if (state == LineComment) {
                if (c == '\n') {
                    state = Normal;
                    clean[i] = '\n';
                } else {
                    clean[i] = ' ';
                }
            } else if (state == BlockComment) {
                if (c == '*' && next == '/') {
                    clean[i] = ' ';
                } else if (i > 0 && data[i - 1] == '*' && c == '/') {
                    state = Normal;
                    clean[i] = ' ';
                } else if (c == '\n') {
                    clean[i] = '\n';
                } else {
                    clean[i] = ' ';
                }
            } else if (state == Preprocessor) {
                if (c == '\n') {
                    state = Normal;
                    clean[i] = '\n';
                } else {
                    clean[i] = ' ';
                }
            } else {
                if (c == '\\' && next != 0) {
                    clean[i] = ' ';
                    if (next == '\n')
                        clean[i + 1] = '\n';
                    else
                        clean[i + 1] = ' ';
                    i++;
                } else if ((state == StringLiteral && c == '"') || (state == CharLiteral && c == '\'')) {
                    state = Normal;
                    clean[i] = ' ';
                } else if (c == '\n') {
                    clean[i] = '\n';
                } else {
                    clean[i] = ' ';
                }
            }

            if (c == '\n')
                line_start = true;
            else if (state == Normal && clean[i] != ' ' && clean[i] != '\t' && clean[i] != '\r')
                line_start = false;
        }

        clean[len] = 0;
        String ret(clean, len);
        delete [] clean;
        return ret;
    }

    CppLint::TokenList CppLint::tokenize(String source) const {
        TokenList tokens;
        const char* data = source.operator char *();
        size_t len = (size_t)source.length();
        int line = 1;

        for (size_t i=0; i<len;) {
            char c = data[i];

            if (c == '\n') {
                line++;
                i++;
                continue;
            }

            if (c == ' ' || c == '\t' || c == '\r') {
                i++;
                continue;
            }

            Token token;
            token.line = line;
            token.identifier = false;

            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
                size_t start = i++;
                while (i < len) {
                    char ch = data[i];
                    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') || ch == '_') {
                        i++;
                    } else {
                        break;
                    }
                }
                token.text = String(&data[start], i - start);
                token.identifier = true;
                tokens.push_back(token);
                continue;
            }

            if (c == ':' && i + 1 < len && data[i + 1] == ':') {
                token.text = String("::");
                i += 2;
            } else if (c == '&' && i + 1 < len && data[i + 1] == '&') {
                token.text = String("&&");
                i += 2;
            } else {
                token.text = String(&data[i], 1);
                i++;
            }

            tokens.push_back(token);
        }

        return tokens;
    }

    void CppLint::lintTokens(const String& path, const TokenList& tokens) {
        for (size_t i=0; i<tokens.size(); i++) {
            if (!isStatementStartToken(tokens, i))
                continue;

            size_t type_end = 0;
            String canonical;
            bool allowed = false;

            if (!parseType(tokens, i, &type_end, &canonical, &allowed, GeneralTypeContext))
                continue;

            bool has_pointer = false;
            bool has_reference = false;
            bool has_const = false;
            size_t after_type = skipPointerReference(tokens, type_end, &has_pointer, &has_reference, &has_const);
            if (after_type >= tokens.size())
                continue;

            if (!isIdentifierToken(tokens[after_type]))
                continue;

            size_t name_index = after_type;
            size_t next_index = name_index + 1;
            if (next_index >= tokens.size())
                continue;

            const String& next = tokens[next_index].text;

            if (next == String("(")) {
                if (!allowed) {
                    addViolation(path, tokens[i].line,
                                 String("forbidden return type '") + canonical + String("'"));
                }

                size_t close_index = 0;
                if (findMatchingParen(tokens, next_index, &close_index))
                    lintFunctionParameters(path, tokens, next_index + 1, close_index);
                continue;
            }

            if (next == String(";") || next == String(",") || next == String("=") ||
                next == String("[") || next == String(":") || next == String(")")) {
                if (!allowed) {
                    addViolation(path, tokens[i].line,
                                 String("forbidden type '") + canonical + String("' for '") + tokens[name_index].text + String("'"));
                }
            }
        }
    }

    void CppLint::lintFunctionParameters(const String& path, const TokenList& tokens, size_t start, size_t end) {
        size_t param_start = start;
        int depth = 0;

        for (size_t i=start; i<=end; i++) {
            bool split = false;
            if (i == end) {
                split = true;
            } else if (tokens[i].text == String("<")) {
                depth++;
            } else if (tokens[i].text == String(">")) {
                if (depth > 0)
                    depth--;
            } else if (depth == 0 && tokens[i].text == String(",")) {
                split = true;
            }

            if (split) {
                lintParameterRange(path, tokens, param_start, i);
                param_start = i + 1;
            }
        }
    }

    void CppLint::lintParameterRange(const String& path, const TokenList& tokens, size_t start, size_t end) {
        while (start < end && (tokens[start].text == String("const") || tokens[start].text == String("volatile")))
            start++;

        if (start >= end)
            return;

        if (tokens[start].text == String("void") && start + 1 == end)
            return;

        if (tokens[start].text == String("..."))
            return;

        size_t type_end = 0;
        String canonical;
        bool allowed = false;
        if (!parseType(tokens, start, &type_end, &canonical, &allowed, ParameterTypeContext))
            return;

        if (!allowed) {
            addViolation(path, tokens[start].line,
                         String("forbidden parameter type '") + canonical + String("'"));
        }
    }

    bool CppLint::isStatementStartToken(const TokenList& tokens, size_t index) const {
        if (index >= tokens.size())
            return false;

        if (!isIdentifierToken(tokens[index]))
            return false;

        if (index == 0)
            return true;

        const String& prev = tokens[index - 1].text;
        if (isDeclarationBoundary(prev))
            return true;

        if (prev == String("public") || prev == String("private") || prev == String("protected"))
            return true;

        return false;
    }

    bool CppLint::isDeclarationBoundary(const String& token) const {
        return token == String(";") ||
               token == String("{") ||
               token == String("}") ||
               token == String("public") ||
               token == String("private") ||
               token == String("protected");
    }

    bool CppLint::isSkippableLeader(const String& token) const {
        return token == String("return") ||
               token == String("new") ||
               token == String("delete") ||
               token == String("throw") ||
               token == String("if") ||
               token == String("else") ||
               token == String("while") ||
               token == String("switch") ||
               token == String("do") ||
               token == String("for") ||
               token == String("case") ||
               token == String("namespace") ||
               token == String("class") ||
               token == String("struct") ||
               token == String("enum") ||
               token == String("typedef") ||
               token == String("using") ||
               token == String("friend");
    }

    bool CppLint::isQualifier(const String& token) const {
        return token == String("const") ||
               token == String("volatile") ||
               token == String("static") ||
               token == String("inline") ||
               token == String("virtual") ||
               token == String("constexpr") ||
               token == String("mutable") ||
               token == String("extern") ||
               token == String("register") ||
               token == String("typename");
    }

    bool CppLint::isAccessSpecifier(const String& token) const {
        return token == String("public") || token == String("private") || token == String("protected");
    }

    bool CppLint::isIdentifierToken(const Token& token) const {
        return token.identifier;
    }

    bool CppLint::isPrimitiveTypeToken(const String& token) const {
        return token == String("int") ||
               token == String("char") ||
               token == String("bool") ||
               token == String("long") ||
               token == String("float") ||
               token == String("double") ||
               token == String("short") ||
               token == String("signed") ||
               token == String("unsigned") ||
               token == String("void");
    }

    bool CppLint::isAllowedPrimitiveSequence(const std::vector<String>& parts) const {
        if (parts.empty())
            return false;

        if (parts.size() == 1) {
            return parts[0] == String("int") ||
                   parts[0] == String("char") ||
                   parts[0] == String("bool") ||
                   parts[0] == String("long") ||
                   parts[0] == String("float") ||
                   parts[0] == String("double") ||
                   parts[0] == String("void");
        }

        if (parts.size() == 2) {
            return (parts[0] == String("unsigned") && (parts[1] == String("int") || parts[1] == String("char") || parts[1] == String("long"))) ||
                   (parts[0] == String("signed") && (parts[1] == String("int") || parts[1] == String("char") || parts[1] == String("long"))) ||
                   (parts[0] == String("long") && parts[1] == String("double")) ||
                   (parts[0] == String("long") && parts[1] == String("long"));
        }

        if (parts.size() == 3) {
            return (parts[0] == String("unsigned") && parts[1] == String("long") && parts[2] == String("long")) ||
                   (parts[0] == String("signed") && parts[1] == String("long") && parts[2] == String("long"));
        }

        return false;
    }

    bool CppLint::isAllowedSafeValueType(String canonical) const {
        return canonical == String("String") ||
               canonical == String("elara::String") ||
               canonical == String("Memory") ||
               canonical == String("elara::Memory") ||
               canonical == String("ByteArray") ||
               canonical == String("elara::ByteArray") ||
               canonical == String("UnitTests") ||
               canonical == String("elara::UnitTests");
    }

    bool CppLint::isAllowedSmartRef(String canonical) const {
        return canonical.startsWith("Ref<") ||
               canonical.startsWith("RefArray<") ||
               canonical.startsWith("elara::Ref<") ||
               canonical.startsWith("elara::RefArray<") ||
               canonical.startsWith("elara::threading::memory::Ref<");
    }

    bool CppLint::isAllowedBorrowedParameterType(String canonical, bool has_reference, bool has_const) const {
        if (!has_reference)
            return false;

        if (canonical == String("UnitTests") || canonical == String("elara::UnitTests"))
            return true;

        if (has_const && isAllowedSafeValueType(canonical))
            return true;

        if (isAllowedSmartRef(canonical))
            return true;

        return false;
    }

    bool CppLint::isAllowedType(const std::vector<String>& parts, String canonical, TypeContext context, bool has_pointer, bool has_reference, bool has_const) const {
        if (context == ParameterTypeContext && isAllowedBorrowedParameterType(canonical, has_reference, has_const))
            return true;

        if (has_pointer)
            return false;

        return isAllowedPrimitiveSequence(parts) ||
               isAllowedSafeValueType(canonical) ||
               isAllowedSmartRef(canonical);
    }

    bool CppLint::parseType(const TokenList& tokens, size_t start, size_t* end, String* canonical, bool* allowed, TypeContext context) const {
        if (start >= tokens.size())
            return false;

        size_t index = start;
        while (index < tokens.size() && isQualifier(tokens[index].text))
            index++;

        if (index >= tokens.size())
            return false;

        if (isSkippableLeader(tokens[index].text) || isAccessSpecifier(tokens[index].text))
            return false;

        std::vector<String> parts;
        size_t type_end = 0;
        String type_canonical;

        if (isPrimitiveTypeToken(tokens[index].text)) {
            parts.push_back(tokens[index].text);
            index++;
            while (index < tokens.size() && isPrimitiveTypeToken(tokens[index].text)) {
                parts.push_back(tokens[index].text);
                index++;
            }

            type_canonical = parts[0];
            for (size_t i=1; i<parts.size(); i++)
                type_canonical += String(" ") + parts[i];
            type_end = index;
        } else if (!parseNamedType(tokens, index, &type_end, &parts, &type_canonical)) {
            return false;
        }

        while (type_end < tokens.size() && (tokens[type_end].text == String("const") || tokens[type_end].text == String("volatile")))
            type_end++;

        bool has_pointer = false;
        bool has_reference = false;
        bool has_const = false;
        size_t final_end = skipPointerReference(tokens, type_end, &has_pointer, &has_reference, &has_const);

        *end = type_end;
        *canonical = type_canonical;
        *allowed = isAllowedType(parts, type_canonical, context, has_pointer, has_reference, has_const);
        *end = final_end;
        return true;
    }

    bool CppLint::parseNamedType(const TokenList& tokens, size_t start, size_t* end, std::vector<String>* parts, String* canonical) const {
        if (start >= tokens.size() || !isIdentifierToken(tokens[start]))
            return false;

        size_t index = start;
        int template_depth = 0;
        String text;
        parts->push_back(tokens[index].text);
        text += tokens[index].text;
        index++;

        while (index < tokens.size()) {
            const String& token = tokens[index].text;

            if (template_depth == 0) {
                if (token == String("::")) {
                    if (index + 1 >= tokens.size() || !isIdentifierToken(tokens[index + 1]))
                        break;
                    text += token;
                    text += tokens[index + 1].text;
                    parts->push_back(tokens[index + 1].text);
                    index += 2;
                    continue;
                }

                if (token == String("<")) {
                    template_depth = 1;
                    text += token;
                    index++;
                    continue;
                }

                break;
            }

            if (isIdentifierToken(tokens[index]) ||
                token == String("::") ||
                token == String(",") ||
                token == String("*") ||
                token == String("&") ||
                token == String("&&") ||
                token == String("(") ||
                token == String(")") ||
                token == String("[") ||
                token == String("]") ||
                token == String("=") ||
                token == String("const") ||
                token == String("volatile")) {
                text += token;
                index++;
                continue;
            }

            if (token == String("<")) {
                template_depth++;
                text += token;
                index++;
                continue;
            }

            if (token == String(">")) {
                template_depth--;
                text += token;
                index++;
                if (template_depth == 0)
                    continue;
                continue;
            }

            break;
        }

        *end = index;
        *canonical = text;
        return true;
    }

    size_t CppLint::skipPointerReference(const TokenList& tokens, size_t index, bool* has_pointer, bool* has_reference, bool* has_const) const {
        *has_pointer = false;
        *has_reference = false;
        *has_const = false;

        while (index < tokens.size()) {
            if (tokens[index].text == String("*")) {
                *has_pointer = true;
                index++;
            } else if (tokens[index].text == String("&") || tokens[index].text == String("&&")) {
                *has_reference = true;
                index++;
            } else if (tokens[index].text == String("const")) {
                *has_const = true;
                index++;
            } else {
                break;
            }
        }
        return index;
    }

    bool CppLint::findMatchingParen(const TokenList& tokens, size_t open_index, size_t* close_index) const {
        int depth = 0;
        for (size_t i=open_index; i<tokens.size(); i++) {
            if (tokens[i].text == String("("))
                depth++;
            else if (tokens[i].text == String(")")) {
                depth--;
                if (depth == 0) {
                    *close_index = i;
                    return true;
                }
            }
        }
        return false;
    }

    void CppLint::addViolation(const String& path, int line, const String& message) {
        Violation violation;
        violation.path = path;
        violation.line = line;
        violation.message = message;
        violations.push_back(violation);
    }

}
