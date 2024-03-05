#include "TestPC.h"
#include <swal/window.h>

namespace {

HINSTANCE hInstance = GetModuleHandle(nullptr);
constexpr DWORD ThisIdx = 0;
enum Timers {
    VideoTimer = 1
};

enum Interrupts {
    VSync = 32
};

const TCHAR ftWndClassName[] = TEXT("TestPC");
const uint8_t startPoint[16] = { 0xEA, 0, 0, 0x40, 0 };

}

TestPC::TestPC() :
    mainMemory(MainMemorySize),
    frameBuffers(FrameBufferSize * 2),
    backBuffer(1),
    cpu(*this),
    window(MyRegisterClass(), hInstance, this)
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
    case WM_TIMER: {
        cpu.SetINTR(VSync);
        window.InvalidateRect();
        break;
    }
    case WM_PAINT: {
        auto dc = wnd.BeginPaint();
        break;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
	}
    return 0;
}

int TestPC::Run()
{
    window.Show(swal::ShowCmd::Show);
    swal::winapi_call(SetTimer(window, VideoTimer, 16, nullptr));
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return msg.wParam;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        auto r = 1; //cpu.Run(1024);
        if (r) {
            swal::winapi_call(WaitMessage());
        }
    }
    return 0;
}

void TestPC::ReadMem(cpu86e::CPUState &state, void *data, size_t size, uint32_t addr)
{
    auto out = static_cast<unsigned char*>(data);
    for (;size != 0; addr = (addr + 1) & 0xFFFFF, ++out, --size) {
        if (addr - ProgramStart < ProgramSize) {
            *out = startPoint[addr - ProgramStart];
        } else if (addr - FrameBufferStart < FrameBufferSize) {
            *out = frameBuffers[backBuffer * FrameBufferSize + addr - FrameBufferStart];
        } else if (addr < MainMemorySize) {
            *out = mainMemory[addr];
        } else {
            *out = 0xFF;
        }
    }
}

void TestPC::WriteMem(cpu86e::CPUState &state, uint32_t addr, void *data, size_t size)
{
    auto in = static_cast<unsigned char*>(data);
    for (;size != 0; addr = (addr + 1) & 0xFFFFF, ++in, --size) {
        if (addr - FrameBufferStart < FrameBufferSize) {
            frameBuffers[backBuffer * FrameBufferSize + addr - FrameBufferStart] = *in;
        } else if (addr < MainMemorySize) {
            mainMemory[addr] = *in;
        }
    }
}

uint8_t TestPC::ReadIOByte(uint32_t addr)
{
    if (addr == 0) {
        return 0;
    }
    return 0xFF;
}

uint16_t TestPC::ReadIOWord(uint32_t addr)
{
    return 0xFFFF;
}

void TestPC::WriteIOByte(uint32_t addr, uint8_t val)
{
    if (addr == 0 && val & 1) {
        cpu.SetINTR(cpu.NoInterrupt);
        backBuffer = !backBuffer;
    }
}

void TestPC::WriteIOWord(uint32_t addr, uint16_t val)
{}
