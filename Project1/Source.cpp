#include "hello_imgui/hello_imgui.h"

#pragma comment(lib, "d3d11")

int main() { 
	HelloImGui::Run([]() { 
		ImGui::Text("Hello, world!"); 
		ImGui::ShowDemoWindow(); 
		}); 

	return 0;

}

int __stdcall WinMain(void*, void*, const char*, int) {
	return main();
}

