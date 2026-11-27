// Test target for signals: dereferences a null pointer (SIGSEGV).
#include <cstdio>

__attribute__((noinline)) void Crash(int* p) {
	*p = 42;								// @crash
}

int main() {
	printf("about to crash\n");
	fflush(stdout);
	Crash(nullptr);
	return 0;
}
