// Test target for how values are shown: the standard library's types (through the engine's visualizers), structures,
// arrays, pointers; and for writing them (values.exe).
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Point {
	int X;
	int Y;
};

struct Shape {
	const char* Name;
	Point Origin;
	Point* Next;
	double Scale;
};

int wmain(int argc, wchar_t* argv[]) {
	std::vector<int> numbers{ 1, 2, 3 };
	std::string text = "hello";
	std::wstring wide = L"wide";
	std::map<int, std::string> names{ { 1, "one" }, { 2, "two" } };
	Point point{ 10, 20 };
	Shape shape{ "square", { 1, 2 }, &point, 1.5 };
	int array[3] = { 7, 8, 9 };
	auto unique = std::make_unique<Point>(Point{ 3, 4 });
	bool flag = true;
	char letter = 'A';
	int count = argc;		// line 32
	wprintf(L"%zu %S %s %zu %d %s %d %d %d %c %d\n", numbers.size(), text.c_str(), wide.c_str(), names.size(), point.X,
		shape.Name, array[1], unique->Y, flag, letter, count);	// line 34
	return 0;
}
