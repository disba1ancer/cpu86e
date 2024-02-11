#include <iostream>
#include <x86emu/cpu.h>

using namespace std;

struct Hook : x86emu::IIOHook {
    unsigned char mem[16] = {
        0x48, 0x06, 4, 0, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144
    };
public:
    void ReadMem(x86emu::CPUState &state, void *data, size_t size, uint32_t addr)
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
    void WriteMem(x86emu::CPUState &state, uint32_t addr, void *data, size_t size)
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
    uint8_t ReadIOByte(uint32_t addr) { return 0xFF; }
    uint16_t ReadIOWord(uint32_t addr) { return 0xFFFF; }
    void WriteIOByte(uint32_t addr, uint8_t val) {}
    void WriteIOWord(uint32_t addr, uint16_t val) {}
};

int main()
{
    Hook hook;
    x86emu::CPU cpu(hook);
    auto& state = cpu.State();
    state.gpr[x86emu::AX] = 0x8000;
    state.sregs[x86emu::CS] = 0;
    cpu.Step();
    cout << hex;
    cout << "AX: " << state.gpr[x86emu::AX] << "\n";
    cout << "CX: " << state.gpr[x86emu::CX] << "\n";
    cout << "DX: " << state.gpr[x86emu::DX] << "\n";
    cout << "BX: " << state.gpr[x86emu::BX] << "\n";
    cout << "SP: " << state.gpr[x86emu::SP] << "\n";
    cout << "BP: " << state.gpr[x86emu::BP] << "\n";
    cout << "SI: " << state.gpr[x86emu::SI] << "\n";
    cout << "DI: " << state.gpr[x86emu::DI] << "\n";
    cout << "FLAGS: " << state.flags << "\n";
    cout << "IP: " << state.ip << "\n";
    cout << "ES: " << state.sregs[x86emu::ES] << "\n";
    cout << "CS: " << state.sregs[x86emu::CS] << "\n";
    cout << "SS: " << state.sregs[x86emu::SS] << "\n";
    cout << "DS: " << state.sregs[x86emu::DS] << "\n";
    cout << "FS: " << state.sregs[x86emu::FS] << "\n";
    cout << "GS: " << state.sregs[x86emu::GS] << "\n";
    cout << "Memory:\n";
    for (int i = 0; i < 16; ++i) {
        cout << int(hook.mem[i]) << " ";
    }
    endl(cout);
    return 0;
}
