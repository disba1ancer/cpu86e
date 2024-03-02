#include <iostream>
#include <cpu86e/cpu.h>

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
    int InterruptCheck() override
    {
        return NoInterrupt;
    }
};

int main()
{
    Hook hook;
    cpu86e::CPU cpu(hook);
    auto& state = cpu.State();
    state.gpr[cpu86e::AX] = 3;
    state.gpr[cpu86e::CX] = 3;
    state.sregs[cpu86e::CS] = 0;
    cpu.Run();
    cout << hex;
    cout << "AX: " << state.gpr[cpu86e::AX] << "\n";
    cout << "CX: " << state.gpr[cpu86e::CX] << "\n";
    cout << "DX: " << state.gpr[cpu86e::DX] << "\n";
    cout << "BX: " << state.gpr[cpu86e::BX] << "\n";
    cout << "SP: " << state.gpr[cpu86e::SP] << "\n";
    cout << "BP: " << state.gpr[cpu86e::BP] << "\n";
    cout << "SI: " << state.gpr[cpu86e::SI] << "\n";
    cout << "DI: " << state.gpr[cpu86e::DI] << "\n";
    cout << "FLAGS: " << state.flags << "\n";
    cout << "IP: " << state.ip << "\n";
    cout << "ES: " << state.sregs[cpu86e::ES] << "\n";
    cout << "CS: " << state.sregs[cpu86e::CS] << "\n";
    cout << "SS: " << state.sregs[cpu86e::SS] << "\n";
    cout << "DS: " << state.sregs[cpu86e::DS] << "\n";
    cout << "FS: " << state.sregs[cpu86e::FS] << "\n";
    cout << "GS: " << state.sregs[cpu86e::GS] << "\n";
    cout << "Memory:\n";
    for (int i = 0; i < 16; ++i) {
        cout << int(hook.mem[i]) << " ";
    }
    endl(cout);
    return 0;
}
