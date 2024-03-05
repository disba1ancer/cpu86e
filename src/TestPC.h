#ifndef TESTPC_H
#define TESTPC_H

#include "cpu86e/cpu.h"
#include <swal/window.h>
#include <cpu86e/iiohook.h>
#include <vector>

class TestPC : public cpu86e::IIOHook
{
public:
    TestPC();
    int Run();

    // IIOHook interface
public:
    void ReadMem(cpu86e::CPUState &state, void *data, size_t size, uint32_t addr);
    void WriteMem(cpu86e::CPUState &state, uint32_t addr, void *data, size_t size);
    uint8_t ReadIOByte(uint32_t addr);
    uint16_t ReadIOWord(uint32_t addr);
    void WriteIOByte(uint32_t addr, uint8_t val);
    void WriteIOWord(uint32_t addr, uint16_t val);

private:
    static ATOM MyRegisterClass();
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    static constexpr
    auto ProgramSize = 0x10;
    static constexpr
    auto ProgramStart = 0xFFFF0;
    static constexpr
    auto MainMemorySize = 0xA0000;
    std::vector<unsigned char> mainMemory;
    static constexpr
    auto FrameBufferStart = 0xA0000;
    static constexpr
    auto FrameBufferSize = 0x10000;
    std::vector<unsigned char> frameBuffers;
    int backBuffer;
    cpu86e::CPU cpu;
    swal::Window window;
    MSG msg;
};

#endif // TESTPC_H
