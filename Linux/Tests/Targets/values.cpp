// Test target for locals, watches, values (pretty-printed standard containers), assignment, memory and registers.
#include <cstdio>
#include <string>
#include <vector>

struct Point {
	int X;
	int Y;
};

struct Shape {
	Point Origin;
	const char* Name;
	Shape* Next;
};

int g_buffer[4] = { 1, 2, 3, 4 };

__attribute__((noinline)) int Use(const Point& p, const std::vector<int>& v, const std::string& s, const Shape& shape) {
	return p.X + static_cast<int>(v.size()) + static_cast<int>(s.size()) + shape.Origin.Y;
}

int main(int argc, char** argv) {
	Point point{ 3, 4 };
	std::vector<int> numbers{ 10, 20, 30 };
	std::string text = "hello";
	Shape second{ { 5, 6 }, "second", nullptr };
	Shape shape{ { 1, 2 }, "first", &second };
	int array[3] = { 7, 8, 9 };
	int sum = Use(point, numbers, text, shape);		// @use
	sum += array[1] + g_buffer[0];
	printf("%d %d\n", sum, point.X);				// @after
	return 0;
}
