#include "CommandLineParser.h"

#include <ctype.h>

namespace elara {

    namespace {

        bool isIdentifierChar(char ch) {
            return isalnum((unsigned char)ch) || ch == '_';
        }

        bool isQualifiedIdentifier(String value) {
            if (!value.length())
                return false;

            bool expect_identifier = true;
            for (int i=0; i<value.length(); i++) {
                char ch = value.operator char *()[i];

                if (ch == '.') {
                    if (expect_identifier)
                        return false;
                    expect_identifier = true;
                    continue;
                }

                if (!isIdentifierChar(ch))
                    return false;

                expect_identifier = false;
            }

            return !expect_identifier;
        }

        bool isSimpleIdentifier(String value) {
            if (!value.length())
                return false;

            for (int i=0; i<value.length(); i++) {
                if (!isIdentifierChar(value.operator char *()[i]))
                    return false;
            }

            return true;
        }

        char decodeEscape(char ch) {
            if (ch == 'n')
                return '\n';
            if (ch == 'r')
                return '\r';
            if (ch == 't')
                return '\t';
            return ch;
        }

        bool parseQuotedParameter(String source, int &offset, String &parameter, String &error) {
            parameter = String();

            if (offset >= source.length() || source.operator char *()[offset] != '"') {
                error = "Quoted parameter must start with a double quote";
                return false;
            }

            offset++;
            while (offset < source.length()) {
                char ch = source.operator char *()[offset++];

                if (ch == '\\') {
                    if (offset >= source.length()) {
                        error = "Quoted parameter ends with an incomplete escape";
                        return false;
                    }

                    parameter += String(decodeEscape(source.operator char *()[offset++]));
                    continue;
                }

                if (ch == '"')
                    return true;

                parameter += String(ch);
            }

            error = "Quoted parameter is missing a closing double quote";
            return false;
        }

        bool parseBareParameter(String source, int &offset, String &parameter, String &error) {
            int start = offset;

            while (offset < source.length() && source.operator char *()[offset] != ',')
                offset++;

            parameter = source.substr(start, offset - start).trim();
            if (!parameter.length()) {
                error = "Empty parameter is not allowed";
                return false;
            }

            return true;
        }

        bool parseParameters(String source, Array<String> &parameters, String &error) {
            parameters.clear();

            int offset = 0;
            while (offset < source.length()) {
                while (offset < source.length() && isspace((unsigned char)source.operator char *()[offset]))
                    offset++;

                if (offset >= source.length())
                    break;

                String parameter;
                if (source.operator char *()[offset] == '"') {
                    if (!parseQuotedParameter(source, offset, parameter, error))
                        return false;
                } else {
                    if (!parseBareParameter(source, offset, parameter, error))
                        return false;
                }

                parameters.push(parameter);

                while (offset < source.length() && isspace((unsigned char)source.operator char *()[offset]))
                    offset++;

                if (offset >= source.length())
                    break;

                if (source.operator char *()[offset] != ',') {
                    error = "Parameters must be separated by commas";
                    return false;
                }

                offset++;

                while (offset < source.length() && isspace((unsigned char)source.operator char *()[offset]))
                    offset++;

                if (offset >= source.length()) {
                    error = "Trailing comma is not allowed";
                    return false;
                }
            }

            return true;
        }

        bool parseWhitespaceToken(String source, int &offset, String &parameter, String &error) {
            if (offset >= source.length()) {
                error = "Missing token";
                return false;
            }

            if (source.operator char *()[offset] == '"')
                return parseQuotedParameter(source, offset, parameter, error);

            int start = offset;
            while (offset < source.length() && !isspace((unsigned char)source.operator char *()[offset]))
                offset++;

            parameter = source.substr(start, offset - start).trim();
            if (!parameter.length()) {
                error = "Empty token is not allowed";
                return false;
            }

            return true;
        }

        bool parseSimple(String command_line, CommandLineInvocation &invocation, String &error) {
            String command = command_line.trim();
            int offset = 0;

            if (!command.length()) {
                error = "Command line is empty";
                return false;
            }

            while (offset < command.length() && isspace((unsigned char)command.operator char *()[offset]))
                offset++;

            if (!parseWhitespaceToken(command, offset, invocation.module_name, error))
                return false;

            while (offset < command.length() && isspace((unsigned char)command.operator char *()[offset]))
                offset++;

            if (offset >= command.length()) {
                error = "Simple command line is missing a method name";
                return false;
            }

            if (!parseWhitespaceToken(command, offset, invocation.method_name, error))
                return false;

            if (!isQualifiedIdentifier(invocation.module_name)) {
                error = "Module name contains invalid characters";
                return false;
            }

            if (!isSimpleIdentifier(invocation.method_name)) {
                error = "Method name contains invalid characters";
                return false;
            }

            invocation.parameters.clear();
            while (offset < command.length()) {
                while (offset < command.length() && isspace((unsigned char)command.operator char *()[offset]))
                    offset++;

                if (offset >= command.length())
                    break;

                String parameter;
                if (!parseWhitespaceToken(command, offset, parameter, error))
                    return false;

                invocation.parameters.push(parameter);
            }

            return true;
        }

        bool parseRpcDefault(String command_line, CommandLineInvocation &invocation, String &error) {
            String command = command_line.trim();
            int open_paren = -1;
            int last_dot = -1;

            if (!command.length()) {
                error = "Command line is empty";
                return false;
            }

            if (command.endsWith(String(";"))) {
                command = command.substr(0, command.length() - 1).trim();
            }

            if (!command.endsWith(String(")"))) {
                error = "Command line must end with a closing parenthesis";
                return false;
            }

            open_paren = command.indexOf(String("("));
            if (open_paren <= 0) {
                error = "Command line must contain module.method(...)";
                return false;
            }

            String target = command.substr(0, open_paren).trim();
            String parameter_source = command.substr(open_paren + 1, command.length() - open_paren - 2);

            for (int i=0; i<target.length(); i++) {
                if (target.operator char *()[i] == '.')
                    last_dot = i;
            }

            if (last_dot <= 0 || last_dot >= target.length() - 1) {
                error = "Command target must be in module.method form";
                return false;
            }

            invocation.module_name = target.substr(0, last_dot).trim();
            invocation.method_name = target.substr(last_dot + 1).trim();

            if (!isQualifiedIdentifier(invocation.module_name)) {
                error = "Module name contains invalid characters";
                return false;
            }

            if (!isSimpleIdentifier(invocation.method_name)) {
                error = "Method name contains invalid characters";
                return false;
            }

            if (!parseParameters(parameter_source, invocation.parameters, error))
                return false;

            return true;
        }

    }

    CommandLineInvocation::CommandLineInvocation() : parameters(4) {
    }

    void CommandLineInvocation::clear() {
        profile_name = String();
        module_name = String();
        method_name = String();
        parameters.clear();
    }

    String CommandLineInvocation::getQualifiedMethod() {
        if (!module_name.length())
            return method_name;
        if (!method_name.length())
            return module_name;
        return String(module_name) + String(".") + method_name;
    }

    bool CommandLineParser::parse(String command_line, CommandLineInvocation &invocation, String &error, String profile_name) {
        invocation.clear();
        invocation.profile_name = profile_name;
        error = String();

        if (profile_name == String("rpc-default"))
            return parseRpcDefault(command_line, invocation, error);

        if (profile_name == String("simple"))
            return parseSimple(command_line, invocation, error);

        if (!(profile_name == String("rpc-default"))) {
            error = String("Unsupported command line profile: %").arg(profile_name);
            return false;
        }
        return false;
    }

}
