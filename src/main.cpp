#include <iostream>
#include <cpu86e/cpu.h>
#include "TestPC.h"

using namespace std;

struct Hook : cpu86e::IIOHook {
    unsigned char mem[16] = {
        0x31, 0xD2, 0x3D, 0x00, 0x00, 0x74, 0x05, 0x01, 0xCA, 0x48, 0x75, 0xFB, 0xF4, 144, 144, 144
    };
public:
    void ReadMem(cpu86e::CPUState &state, void *data, size_t size, uint32_t addr) override
    {
        auto out = reinterpret_cast<unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            auto cAddr = addr + i;
            if (cAddr < 16) {
                out[i] = mem[cAddr];
            } else {
                out[i] = 0xFF;
            }
        }
    }
    void WriteMem(cpu86e::CPUState &state, uint32_t addr, void *data, size_t size) override
    {
        auto in = reinterpret_cast<unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            auto cAddr = addr + i;
            if (cAddr >= 16) {
                break;
            }
            mem[cAddr] = in[i];
        }
    }
    uint8_t ReadIOByte(uint32_t addr) override { return 0xFF; }
    uint16_t ReadIOWord(uint32_t addr) override { return 0xFFFF; }
    void WriteIOByte(uint32_t addr, uint8_t val) override {}
    void WriteIOWord(uint32_t addr, uint16_t val) override {}
};

int main()
{
    TestPC testPC;
    return testPC.Run();
}
