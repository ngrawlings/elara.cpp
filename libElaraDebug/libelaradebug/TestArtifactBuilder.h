//
//  TestArtifactBuilder.h
//  libElaraDebug
//

#ifndef TestArtifactBuilder_h
#define TestArtifactBuilder_h

#include <libelaracore/memory/LinkedList.h>
#include <libelaracore/memory/Ref.h>
#include <libelaracore/memory/String.h>

namespace elara {

    typedef struct {
        String key;
        String value;
    } TEST_ARTIFACT_ENTRY;

    class TestArtifactBuilder {
    public:
        TestArtifactBuilder();
        virtual ~TestArtifactBuilder();

        void setRootPath(String root_path);
        void setRunMode(String mode);
        void addMetadata(String key, String value);

        bool startRun(int test_count);
        void recordTestResult(String test_name, bool success, String detail, long long duration_us);
        void finishRun(bool success, int passed, int failed, long long duration_us);

        String getRunDirectory() const;

    private:
        String root_path;
        String run_mode;
        String run_directory;
        LinkedList< Ref<TEST_ARTIFACT_ENTRY> > metadata;

        static String sanitizePath(String value);
        static bool ensureDirectory(String path);
        static bool writeTextFile(String path, String contents);
        static String buildTimestamp();
    };

}

#endif /* TestArtifactBuilder_h */
