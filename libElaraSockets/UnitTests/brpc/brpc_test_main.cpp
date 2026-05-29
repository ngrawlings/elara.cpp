//
//  brpc_test_main.cpp
//  Standalone runner: BRpc unit tests + benchmark vs JSON RPC
//
//  Compile:
//    g++ -std=c++17 -O2 -I. -I../../ -I../../../build/include \
//        brpc_test_main.cpp BRpcTests.cpp \
//        -L../../../build/lib -lelarasockets -lelaracore \
//        -o brpc_test_main
//

#include "BRpcTests.h"
#include <libelarasockets/rpc/brpc/BRpcCodec.h>
#include <libelarasockets/rpc/json/JsonRPCCodec.h>
#include <libelaracore/memory/String.h>
#include <libelaracore/memory/ByteArray.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

using namespace elara;
using namespace elara::sockets::rpc::brpc;
using namespace elara::sockets::rpc::json;

// ── Minimal test harness ──────────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// BRpcTests.cpp calls UnitTests::fail(); satisfy it via the debug library.
#include <libelaradebug/UnitTests.h>

static void run_test(const char* name, bool(*fn)()) {
    tests_run++;
    printf("  %-45s", name);
    fflush(stdout);
    bool ok = false;
    try {
        ok = fn();
    } catch (const String& err) {
        printf("\n    detail: %s\n  %-45s", (const char*)err, "");
    } catch (...) {
        printf("\n    detail: unknown exception\n  %-45s", "");
    }
    if (ok) {
        tests_passed++;
        printf("PASS\n");
    } else {
        tests_failed++;
        printf("FAIL\n");
    }
}

// ── Timing ────────────────────────────────────────────────────────────────────

static double now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ── Benchmark helpers ─────────────────────────────────────────────────────────

// Represents a realistic RPC message:
//   { "method": "ui.setSectionJson",
//     "id": 42,
//     "target": "nav.debug.cpp_stack",
//     "ok": 1,
//     "ts": 1700000000123 }

static const char* BENCH_METHOD  = "ui.setSectionJson";
static const char* BENCH_TARGET  = "nav.debug.cpp_stack";
static const int   BENCH_ID      = 42;
static const int   BENCH_OK      = 1;
static const long long BENCH_TS  = 1700000000123LL;

// JSON encode one message
static ByteArray json_encode() {
    String params = String("{\"target\":\"") + String(BENCH_TARGET)
        + String("\",\"id\":") + String(BENCH_ID)
        + String(",\"ok\":") + String(BENCH_OK)
        + String(",\"ts\":") + String((long long)BENCH_TS)
        + String("}");
    String msg = JsonRPCCodec::buildRequest(String("42"), String(BENCH_METHOD), params);
    return JsonRPCCodec::framePayload(msg);
}

// JSON decode one framed message
static bool json_decode(const ByteArray& framed) {
    // Strip the 4-byte length prefix, pass body to codec
    if (framed.length() < 5) return false;
    const char* body_ptr = (const char*)framed + 4;
    int body_len = framed.length() - 4;
    String body(body_ptr, body_len);

    String id, method, params_json, err;
    if (!JsonRPCCodec::parseRequest(body, id, method, params_json, err))
        return false;

    // Parse the inner params to extract "target"
    String target;
    JsonRPCCodec::getStringField(params_json, String("target"), target);
    return target.length() > 0;
}

// BRpc encode one message (framed with 4-byte big-endian length prefix)
static ByteArray brpc_encode() {
    BRpcWriter fields;
    fields.writeNamedString(String("method"), String(BENCH_METHOD));
    fields.writeNamedString(String("target"), String(BENCH_TARGET));
    fields.writeNamedInt(String("id"),   BENCH_ID);
    fields.writeNamedByte(String("ok"),  (uint8_t)BENCH_OK);
    fields.writeNamedLong(String("ts"),  (int64_t)BENCH_TS);

    BRpcWriter msg;
    msg.writeArray(fields, 5);

    const ByteArray& payload = msg.bytes();
    uint32_t len = (uint32_t)payload.length();
    uint8_t prefix[4] = {
        (uint8_t)(len >> 24), (uint8_t)(len >> 16),
        (uint8_t)(len >>  8), (uint8_t)(len      ),
    };
    ByteArray framed;
    framed.append(prefix, 4);
    framed.append(payload);
    return framed;
}

// BRpc decode one framed message
static bool brpc_decode(const ByteArray& framed) {
    if (framed.length() < 5) return false;
    const char* body_ptr = (const char*)framed + 4;
    size_t body_len = (size_t)(framed.length() - 4);

    BRpcReader r(body_ptr, body_len);
    uint32_t total, count;
    if (!r.readArrayHeader(total, count)) return false;

    String name, target;
    bool found_target = false;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t type;
        if (!r.peekType(type)) return false;
        if (type == BRPC_NAMED_STRING) {
            String v;
            if (!r.readNamedString(name, v)) return false;
            if (memcmp((const char*)name, "target", 6) == 0)
                found_target = true;
        } else {
            r.skipValue();
        }
    }
    return found_target;
}

static void bench(const char* label, ByteArray(*encode_fn)(),
                  bool(*decode_fn)(const ByteArray&), int iterations)
{
    // Warmup
    for (int i = 0; i < 1000; i++) {
        ByteArray b = encode_fn();
        (void)decode_fn(b);
    }

    // Encode
    double t0 = now_ms();
    for (int i = 0; i < iterations; i++) {
        ByteArray b = encode_fn();
        (void)b;
    }
    double encode_ms = now_ms() - t0;

    // Decode (pre-encode one sample)
    ByteArray sample = encode_fn();
    double t1 = now_ms();
    for (int i = 0; i < iterations; i++) {
        bool ok = decode_fn(sample);
        (void)ok;
    }
    double decode_ms = now_ms() - t1;

    // Wire size
    ByteArray wire = encode_fn();
    int wire_bytes = (int)wire.length();

    printf("  %-8s  encode: %7.2f ms  decode: %7.2f ms  "
           "wire: %d bytes  (%.0f kops/s encode  %.0f kops/s decode)\n",
           label,
           encode_ms, decode_ms,
           wire_bytes,
           (double)iterations / encode_ms,
           (double)iterations / decode_ms);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    printf("\n══════════════════════════════════════════════════════\n");
    printf("  BRpc Unit Tests\n");
    printf("══════════════════════════════════════════════════════\n\n");

    run_test("brpc_roundtrip_byte",           brpc_roundtrip_byte);
    run_test("brpc_roundtrip_short",          brpc_roundtrip_short);
    run_test("brpc_roundtrip_int",            brpc_roundtrip_int);
    run_test("brpc_roundtrip_long",           brpc_roundtrip_long);
    run_test("brpc_roundtrip_string",         brpc_roundtrip_string);
    run_test("brpc_roundtrip_empty_string",   brpc_roundtrip_empty_string);
    run_test("brpc_roundtrip_named_byte",     brpc_roundtrip_named_byte);
    run_test("brpc_roundtrip_named_short",    brpc_roundtrip_named_short);
    run_test("brpc_roundtrip_named_int",      brpc_roundtrip_named_int);
    run_test("brpc_roundtrip_named_long",     brpc_roundtrip_named_long);
    run_test("brpc_roundtrip_named_string",   brpc_roundtrip_named_string);
    run_test("brpc_roundtrip_array_unnamed",  brpc_roundtrip_array_unnamed);
    run_test("brpc_roundtrip_object",         brpc_roundtrip_object);
    run_test("brpc_roundtrip_nested_arrays",  brpc_roundtrip_nested_arrays);
    run_test("brpc_roundtrip_empty_array",    brpc_roundtrip_empty_array);
    run_test("brpc_roundtrip_mixed_object",   brpc_roundtrip_mixed_object);
    run_test("brpc_sequential_reads",         brpc_sequential_reads);
    run_test("brpc_skip_value",               brpc_skip_value);
    run_test("brpc_boundary_values",          brpc_boundary_values);
    run_test("brpc_truncated_input",          brpc_truncated_input);
    run_test("brpc_wrong_type_rejected",      brpc_wrong_type_rejected);
    run_test("brpc_wire_layout_byte",         brpc_wire_layout_byte);
    run_test("brpc_wire_layout_array_header", brpc_wire_layout_array_header);

    printf("\n  %d/%d passed", tests_passed, tests_run);
    if (tests_failed)
        printf("  (%d FAILED)", tests_failed);
    printf("\n");

    if (tests_failed) {
        printf("\nUnit tests failed — skipping benchmark.\n\n");
        return 1;
    }

    // ── Benchmark ─────────────────────────────────────────────────────────────

    const int N = 500000;
    printf("\n══════════════════════════════════════════════════════\n");
    printf("  Benchmark  (%d iterations per codec)\n", N);
    printf("  Message: { method, target, id(int), ok(byte), ts(long) }\n");
    printf("══════════════════════════════════════════════════════\n\n");

    printf("  Wire sizes (preview):\n");
    {
        ByteArray jw = json_encode();
        ByteArray bw = brpc_encode();
        printf("    JSON  framed: %d bytes\n", (int)jw.length());
        printf("    BRpc  framed: %d bytes\n", (int)bw.length());
        printf("    Ratio: %.1fx smaller\n\n", (double)jw.length() / bw.length());
    }

    bench("JSON",  json_encode,  json_decode,  N);
    bench("BRpc",  brpc_encode,  brpc_decode,  N);

    printf("\n");
    return 0;
}
