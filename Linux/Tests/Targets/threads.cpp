// Test target for threads: two named workers that wait at a barrier while main is at its breakpoint; also loops for ever
// with "forever" (the attach test).
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>

std::atomic<int> g_ready;
std::atomic<bool> g_done;

void Worker(const char* name) {
	pthread_setname_np(pthread_self(), name);
	g_ready++;
	while (!g_done)
		usleep(1000);						// @worker
}

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "forever") == 0) {
		// (any debugger may attach, also where Yama's ptrace_scope is 1, as on Ubuntu)
		prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
		for (;;)
			usleep(10000);					// @forever
	}
	std::thread a(Worker, "worker-a"), b(Worker, "worker-b");
	while (g_ready < 2)
		usleep(1000);
	g_done = true;							// @all-ready
	a.join();
	b.join();
	return 0;
}
