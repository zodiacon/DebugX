// Test target for breakpoint conditions / pass counts / data breakpoints.
#include <cstdio>

volatile int g_iter;
volatile int g_count;

__declspec(noinline) void Work(int i) {
	int doubled = i * 2;
	g_count += doubled;		// line 9
}

int main() {
	for (int i = 0; i < 10; i++) {
		g_iter = i;
		Work(i);
	}
	printf("%d\n", g_count);
	return 0;
}
