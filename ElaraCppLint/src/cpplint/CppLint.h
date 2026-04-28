#ifndef ELARA_CPP_LINT_H
#define ELARA_CPP_LINT_H

#include <libelaracore/memory/String.h>

#include <vector>

namespace elara {

    class CppLint {
    public:
        CppLint();

        int run(int argc, const char* const* argv);

    private:
        struct Violation {
            String path;
            int line;
            String message;
        };

        struct Token {
            String text;
            int line;
            bool identifier;
        };

        typedef std::vector<Token> TokenList;

        std::vector<String> input_paths;
        std::vector<String> files;
        std::vector<Violation> violations;

        bool parseArguments(int argc, const char* const* argv);
        void printUsage(const char* argv0) const;
        bool collectFiles();
        bool collectPath(const String& path);
        bool lintFile(const String& path);
        bool isCppPath(String path) const;
        bool shouldSkipDirectory(String path) const;

        String readFile(String path, bool* ok) const;
        String sanitizeSource(String source) const;
        TokenList tokenize(String source) const;
        void lintTokens(const String& path, const TokenList& tokens);
        void lintFunctionParameters(const String& path, const TokenList& tokens, size_t start, size_t end);
        void lintParameterRange(const String& path, const TokenList& tokens, size_t start, size_t end);

        bool isStatementStartToken(const TokenList& tokens, size_t index) const;
        bool isDeclarationBoundary(const String& token) const;
        bool isSkippableLeader(const String& token) const;
        bool isQualifier(const String& token) const;
        bool isAccessSpecifier(const String& token) const;
        bool isIdentifierToken(const Token& token) const;
        bool isPrimitiveTypeToken(const String& token) const;
        bool isAllowedPrimitiveSequence(const std::vector<String>& parts) const;
        bool isAllowedSafeValueType(String canonical) const;
        bool isAllowedSmartRef(String canonical) const;
        bool isAllowedType(const std::vector<String>& parts, String canonical) const;

        bool parseType(const TokenList& tokens, size_t start, size_t* end, String* canonical, bool* allowed) const;
        bool parseNamedType(const TokenList& tokens, size_t start, size_t* end, std::vector<String>* parts, String* canonical) const;
        size_t skipPointerReference(const TokenList& tokens, size_t index) const;
        bool findMatchingParen(const TokenList& tokens, size_t open_index, size_t* close_index) const;

        void addViolation(const String& path, int line, const String& message);
    };

}

#endif
