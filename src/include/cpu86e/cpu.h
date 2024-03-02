#ifndef CPU86E_CPU_H
#define CPU86E_CPU_H

#include "iiohook.h"
#include <cstdint>

namespace cpu86e {

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;

using RegVal = uint16_t;

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

struct CPUState {
    RegVal gpr[8];
    RegVal ip;
    RegVal flags;
    uint16_t sregs[6];
};

class CPU
{
public:
    CPU(IIOHook& hook);
    CPU(const CPUState& initState, IIOHook& hook);
    void StoreState(CPUState& initState) const;
    void LoadState(const CPUState& initState);
    auto State() -> CPUState&;
    auto State() const -> const CPUState&;
    void SetHook(IIOHook* hook);
    void Run();
    void Step();
    void InitInterrupt(int interrupt);
    static
    void SetInitState(CPUState&state);
private:
    struct Prefixes;
    struct Operations;
    struct Calc;
    int DoOpcode();
    auto ParsePrefixes() -> Prefixes;
    auto ReadByte(SegmentRegister sreg, uint16_t addr) -> uint8_t;
    auto ReadWord(SegmentRegister sreg, uint16_t addr) -> uint16_t;
    auto ReadMem(SegmentRegister sreg, uint16_t addr, int logSz) -> RegVal;
    void WriteByte(SegmentRegister sreg, uint16_t addr, uint8_t val);
    void WriteWord(SegmentRegister sreg, uint16_t addr, uint16_t val);
    void WriteMem(SegmentRegister sreg, uint16_t addr, int logSz, RegVal val);
    auto ReadByte(uint32_t addr) -> uint8_t;
    auto ReadWord(uint32_t addr) -> uint16_t;
    auto ReadMem(uint32_t addr, int logSz) -> RegVal;
    void WriteByte(uint32_t addr, uint8_t val);
    void WriteWord(uint32_t addr, uint16_t val);
    void WriteMem(uint32_t addr, int logSz, RegVal val);
    auto CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t;

    CPUState state;
    IIOHook* hook;
};

} // namespace x86emu

#endif // CPU86E_CPU_H