// Test target for exception filters: each exception is handled by the target itself.
#include <windows.h>
#include <cstdio>

volatile int g_stage;

__declspec(noinline) void Cpp() {
	try { throw 42; } catch (int) {}
}

__declspec(noinline) void AccessViolation() {
	__try { *(volatile int*)nullptr = 1; } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline) void Custom() {
	__try { ::RaiseException(0xE0001234, 0, 0, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

int main() {
	g_stage = 1;
	Cpp();
	g_stage = 2;
	AccessViolation();
	g_stage = 3;
	Custom();
	g_stage = 4;
	printf("done\n");
	return 0;
}
