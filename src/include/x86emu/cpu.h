#ifndef X86EMU_CPU_H
#define X86EMU_CPU_H

#include "x86emu/iiohook.h"
#include <cstdint>

namespace x86emu {

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;

enum Register {
    AX,
    CX,
    DX,
    BX,
    SP,
    BP,
    SI,
    DI,
};

enum SegmentRegister {
    ES,
    CS,
    SS,
    DS,
    FS,
    GS,
    SegReserve
};

enum Opcode {
    WordBit = 1,
    InverseBit = 2,
};

struct CPUState {
    uint16_t gpr[8];
    uint16_t sregs[6];
    uint16_t ip;
    uint16_t flags;
};

class CPU
{
public:
    CPU();
    CPU(const CPUState& initState);
    void StoreState(CPUState& initState) const;
    void LoadState(const CPUState& initState);
    auto State() -> CPUState&;
    auto State() const -> const CPUState&;
    void SetHook(IIOHook* hook);
    void Run();
private:
    struct Prefixes;
    int DoOpcode();
    auto ParsePrefixes() -> Prefixes;
    auto ReadByte(SegmentRegister sreg, uint16_t addr) -> uint8_t;
    void PushSreg(SegmentRegister sreg);
    void PopSreg(SegmentRegister sreg);
    auto CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t;

    CPUState state = {};
    IIOHook* hook;
};

} // namespace x86emu

#endif // X86EMU_CPU_H
