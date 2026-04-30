//
//  TestArtifactBuilder.cpp
//  libElaraDebug
//

#include "TestArtifactBuilder.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libelaraio/File.h>

namespace elara {

    TestArtifactBuilder::TestArtifactBuilder() : root_path("./artifacts"), run_mode("unit-tests") {
    }

    TestArtifactBuilder::~TestArtifactBuilder() {
    }

    void TestArtifactBuilder::setRootPath(String root_path) {
        this->root_path = root_path;
    }

    void TestArtifactBuilder::setRunMode(String mode) {
        run_mode = mode;
    }

    void TestArtifactBuilder::addMetadata(String key, String value) {
        Ref<TEST_ARTIFACT_ENTRY> entry(new TEST_ARTIFACT_ENTRY);
        entry->key = key;
        entry->value = value;
        metadata.add(entry);
    }

    bool TestArtifactBuilder::startRun(int test_count) {
        if (!ensureDirectory(root_path))
            return false;

        run_directory = String("%/%-%")
            .arg(root_path)
            .arg(buildTimestamp())
            .arg(sanitizePath(run_mode));

        if (!ensureDirectory(run_directory))
            return false;

        String manifest;
        manifest.append(String("mode=%\n").arg(run_mode));
        manifest.append(String("pid=%\n").arg((int)getpid()));
        manifest.append(String("test_count=%\n").arg(test_count));

        LinkedListState< Ref<TEST_ARTIFACT_ENTRY> > state(&metadata);
        Ref<TEST_ARTIFACT_ENTRY> *entry;
        while (state.iterate(&entry)) {
            manifest.append(String("%=%\n")
                .arg(entry->getPtr()->key)
                .arg(entry->getPtr()->value));
        }

        return writeTextFile(run_directory + "/manifest.txt", manifest);
    }

    void TestArtifactBuilder::recordTestResult(String test_name, bool success, String detail, long long duration_us) {
        if (!run_directory.length())
            return;

        String out;
        out.append(String("name=%\n").arg(test_name));
        out.append(String("status=%\n").arg(success ? "passed" : "failed"));
        out.append(String("duration_us=%\n").arg(duration_us));
        out.append(String("detail=%\n").arg(detail.length() ? detail : String("-")));

        writeTextFile(run_directory + "/" + sanitizePath(test_name) + ".txt", out);
    }

    void TestArtifactBuilder::finishRun(bool success, int passed, int failed, long long duration_us) {
        if (!run_directory.length())
            return;

        String summary;
        summary.append(String("mode=%\n").arg(run_mode));
        summary.append(String("status=%\n").arg(success ? "passed" : "failed"));
        summary.append(String("passed=%\n").arg(passed));
        summary.append(String("failed=%\n").arg(failed));
        summary.append(String("duration_us=%\n").arg(duration_us));

        writeTextFile(run_directory + "/summary.txt", summary);
        writeTextFile(root_path + "/latest.txt", run_directory + "\n");
    }

    String TestArtifactBuilder::getRunDirectory() const {
        return run_directory;
    }

    String TestArtifactBuilder::sanitizePath(String value) {
        String out(value);
        ssize_t len = out.length();
        for (int i=0; i<len; i++) {
            char c = out.byteAt(i);
            bool ok = (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || c == '-'
                || c == '_'
                || c == '.';
            if (!ok)
                c = '_';
        }
        return out;
    }

    bool TestArtifactBuilder::ensureDirectory(String path) {
        if (!path.length())
            return false;

        String current;
        ssize_t len = path.length();
        for (int i=0; i<len; i++) {
            current += path[i];
            if (path.byteAt(i) != '/' && i != len-1)
                continue;

            if (current.length() == 1 && current[0] == '/')
                continue;

            String dir = current;
            if (dir.endsWith("/"))
                dir = dir.substr(0, (int)dir.length()-1);

            if (!dir.length())
                continue;

            if (mkdir((char*)dir, 0775) && errno != EEXIST)
                return false;
        }

        return true;
    }

    bool TestArtifactBuilder::writeTextFile(String path, String contents) {
        try {
            File file(path);
            file.truncate();
            file.write(0, (char*)contents, contents.length());
            return true;
        } catch (...) {
            return false;
        }
    }

    String TestArtifactBuilder::buildTimestamp() {
        time_t now = time(0);
        struct tm tm_now;
        localtime_r(&now, &tm_now);

        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_now);
        return String(buf);
    }

}
