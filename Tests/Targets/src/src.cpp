// Test target for source stepping: calls into code without line information (an import).
#include <Windows.h>
#include <cstdio>

volatile int g_value;

__declspec(noinline) int Twice(int x) {
	return x * 2;		// line 8
}

int main() {
	DWORD ticks = GetTickCount();		// line 12
	g_value = Twice((int)GetTickCount() & 0xff);		// line 13
	printf("%d %u\n", g_value, ticks);	// line 14
	return 0;
}
