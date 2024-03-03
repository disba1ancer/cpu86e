#include "TestPC.h"
#include <swal/window.h>

namespace {

HINSTANCE hInstance = GetModuleHandle(nullptr);
constexpr DWORD ThisIdx = 0;
const TCHAR ftWndClassName[] = TEXT("ftwnd");

}

TestPC::TestPC() :
    programMemory(ProgramSize),
    mainMemory(MainMemorySize),
    frameBuffers(FrameBufferSize * 2),
    frontBuffer(0),
    backBuffer(1),
    cpu(*this)
{}

ATOM TestPC::MyRegisterClass()
{
	auto regCls = []{
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(wcex);
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = swal::ClsWndProc<TestPC, &TestPC::WndProc, ThisIdx>;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = sizeof(TestPC*);
		wcex.hInstance = hInstance;
		wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = ftWndClassName;
		wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
		return swal::winapi_call(RegisterClassEx(&wcex));
	};
	static ATOM cls = regCls();
	return cls;
}

LRESULT TestPC::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    swal::Wnd wnd = hWnd;
	switch (message) {
		case WM_PAINT: {
            auto dc = wnd.BeginPaint();
			break;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}
    return 0;
}

bool TestPC::HandleMessages()
{
    return true;
}

int TestPC::Run()
{
    return 0;
}

void TestPC::ReadMem(cpu86e::CPUState &state, void *data, size_t size, uint32_t addr)
{
    auto out = static_cast<unsigned char*>(data);
    for (;size != 0; addr = (addr + 1) & 0xFFFFF, ++out, --size) {
        if (addr - ProgramStart < ProgramSize) {
            *out = programMemory[addr - ProgramStart];
        } else if (addr - FrameBufferStart < FrameBufferSize) {
            *out = programMemory[addr - FrameBufferStart];
        } else if (addr < MainMemorySize) {
            *out = programMemory[addr];
        }
    }
}

void TestPC::WriteMem(cpu86e::CPUState &state, uint32_t addr, void *data, size_t size)
{
    auto in = static_cast<unsigned char*>(data);
    for (;size != 0; addr = (addr + 1) & 0xFFFFF, ++in, --size) {
        if (addr - ProgramStart < ProgramSize) {
            programMemory[addr - ProgramStart] = *in;
        } else if (addr - FrameBufferStart < FrameBufferSize) {
            programMemory[addr - FrameBufferStart] = *in;
        } else if (addr < MainMemorySize) {
            programMemory[addr] = *in;
        }
    }
}

uint8_t TestPC::ReadIOByte(uint32_t addr)
{
    return 0xFF;
}

uint16_t TestPC::ReadIOWord(uint32_t addr)
{
    return 0xFFFF;
}

void TestPC::WriteIOByte(uint32_t addr, uint8_t val)
{}

void TestPC::WriteIOWord(uint32_t addr, uint16_t val)
{}

int TestPC::InterruptCheck()
{
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return Halt;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return NoInterrupt;
}
