// Test target for breakpoints, conditions, pass counts, data breakpoints, stepping and output.
// GdbEngineTests finds its lines by the @markers.
#include <cstdio>

volatile int g_iter;
volatile int g_count;

__attribute__((noinline)) int Twice(int i) {
	return i * 2;							// @twice
}

__attribute__((noinline)) void Work(int i) {
	int doubled = Twice(i);					// @call-twice
	g_count += doubled;						// @work
}

int main() {
	for (int i = 0; i < 10; i++) {
		g_iter = i;
		Work(i);							// @call-work
	}
	printf("count=%d\n", g_count);			// @print
	return 0;
}
