#include "browser_cef_gate.h"

#include <cstdio>

static int g_failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    }
}

int main() {
    BrowserCefGate gate;

    int calls = 0;
    gate.set_acquire_fn([&calls]() {
        ++calls;
        return calls >= 3;
    });

    bool r1 = gate.ensure_acquired();
    check(!r1, "first acquire attempt fails");
    check(gate.consume_should_log_failure(), "first failure requests logging");
    check(!gate.consume_should_log_failure(), "failure logging is one-shot");

    bool r2 = gate.ensure_acquired();
    check(!r2, "second acquire attempt fails");
    check(gate.consume_should_log_failure(), "second failure requests logging");

    bool r3 = gate.ensure_acquired();
    check(r3, "third acquire attempt succeeds");
    check(gate.is_acquired(), "gate marked as acquired");
    check(!gate.consume_should_log_failure(), "no failure logging after success");

    bool r4 = gate.ensure_acquired();
    check(r4, "acquire remains true after success");
    check(calls == 3, "acquire function not called again once acquired");

    if (g_failures == 0) {
        std::fprintf(stderr, "PASS: test_browser_cef_gate\n");
    }
    return g_failures == 0 ? 0 : 1;
}

