#include "TestPC.h"
#include <swal/window.h>
#include <fstream>
#include <dwmapi.h>

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
{
    std::fill(frameBuffers.begin(), frameBuffers.end(), 0);
    std::ifstream image("testpc.img", std::ios::binary);
    if (image.fail()) {
        throw std::runtime_error("FAIL!");
    }
    image.read(reinterpret_cast<char*>(mainMemory.data()), MainMemorySize);
}

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
		wcex.hbrBackground = NULL; //reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
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
    case WM_USER + 20: {
        cpu.SetINTR(VSync);
        window.InvalidateRect(false);
        break;
    }
    case WM_PAINT: {
        auto dc = wnd.BeginPaint();
        auto& bmih = bmi.header;
        bmih.biSize = sizeof(bmih);
        bmih.biWidth = 320;
        bmih.biHeight = -200;
        bmih.biPlanes = 1;
        bmih.biBitCount = 8;
        bmih.biCompression = BI_RGB;
        bmih.biSizeImage = 0;
        bmih.biXPelsPerMeter = 3780;
        bmih.biYPelsPerMeter = 3780;
        bmih.biClrUsed = 0;
        bmih.biClrImportant = 0;
        for (int i = 0; i < 256; ++i) {
            unsigned char* color = frameBuffers.data() + !backBuffer * FrameBufferSize + 64000 + i * 4;
            bmi.palete[i].rgbBlue = color[0];
            bmi.palete[i].rgbGreen = color[1];
            bmi.palete[i].rgbRed = color[2];
        }
        StretchDIBits(dc, 0, 0, 640, 400, 0, 0, 320, 200, frameBuffers.data() + !backBuffer * FrameBufferSize, reinterpret_cast<BITMAPINFO*>(&bmi), DIB_RGB_COLORS, SRCCOPY);
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
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return msg.wParam;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        auto r = cpu.Run(1024);
        if (r) {
            DwmFlush();
            SendMessage(window, WM_USER + 20, 0, 0);
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
