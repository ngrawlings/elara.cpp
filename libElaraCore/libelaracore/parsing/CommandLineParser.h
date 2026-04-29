#ifndef ElaraCore_CommandLineParser_h
#define ElaraCore_CommandLineParser_h

#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/String.h>

namespace elara {

    class CommandLineInvocation {
    public:
        CommandLineInvocation();

        void clear();
        String getQualifiedMethod();

        String profile_name;
        String module_name;
        String method_name;
        Array<String> parameters;
    };

    class CommandLineParser {
    public:
        static bool parse(String command_line, CommandLineInvocation &invocation, String &error, String profile_name = String("rpc-default"));
    };

}

namespace elara {
namespace core {
namespace parsing {
    using ::elara::CommandLineInvocation;
    using ::elara::CommandLineParser;
}
}
}

#endif
