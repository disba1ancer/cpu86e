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

struct CPUState {
    uint16_t gpr[8];
    uint16_t sregs[6];
    uint16_t ip;
    uint16_t flags;
};

struct FlagsCache {
    uint16_t a, b, r;
    enum Type {
        None,
        Logical,
        Arithmetic,
    } type;
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
private:
    struct Prefixes;
    struct Operations;
    int DoOpcode();
    auto ParsePrefixes() -> Prefixes;
    auto ReadByte(SegmentRegister sreg, uint16_t addr) -> uint8_t;
    auto ReadWord(SegmentRegister sreg, uint16_t addr) -> uint16_t;
    void WriteByte(SegmentRegister sreg, uint16_t addr, uint8_t val);
    void WriteWord(SegmentRegister sreg, uint16_t addr, uint16_t val);
    void PushSreg(SegmentRegister sreg);
    void PopSreg(SegmentRegister sreg);
    auto CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t;
    void FlushFlags();

    CPUState state;
    FlagsCache flagsCache;
    IIOHook* hook;
};

} // namespace x86emu

#endif // X86EMU_CPU_H
