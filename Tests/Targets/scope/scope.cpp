// Test target for locals, watches, scope frames, and writing memory and registers.
#include <cstdio>

__declspec(noinline) int Helper(int value) {
	int squared = value * value;
	return squared + 1;		// line 6
}

int wmain(int argc, wchar_t* argv[]) {
	int total = argc;		// line 10
	total += Helper(argc);	// line 11
	wprintf(L"%s %d\n", argv[0], total);
	return 0;
}
