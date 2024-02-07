#include "include/x86emu/cpu.h"

namespace x86emu {

namespace {

enum Prefix {
    P66 = 1,
    P67 = 2,
    PF0 = 4,
    PF2 = 8,
    PF3 = 16
};

enum DoOpcodeResult {
    Normal,
    Halt,
};

char8_t map[] = {
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1,
};

bool GetParity(uint16_t val)
{
    val &= 0xFF;
    val = (val & 0xF) ^ ((val & 0xF0) >> 4);
    val = (val & 0x3) ^ ((val & 0xC) >> 2);
    return (val & 0x1) ^ ((val & 0x2) >> 1) ^ 1;
}

}

CPU::CPU(IIOHook& hook) :
    CPU({.sregs = {0, 0xFFFF}, .ip = 0}, hook)
{}

CPU::CPU(const CPUState &initState, IIOHook& hook) :
    state(initState),
    flagsCache({}),
    hook(&hook)
{}

void CPU::StoreState(CPUState &initState) const
{
    initState = state;
}

void CPU::LoadState(const CPUState &initState)
{
    state = initState;
}

auto CPU::State() -> CPUState&
{
    return state;
}

auto CPU::State() const -> const CPUState&
{
    return state;
}

void CPU::SetHook(IIOHook *argHook)
{
    hook = argHook;
}

struct CPU::Prefixes {
    unsigned segment:3;
    unsigned prefix:5;
};

void CPU::Run()
{

}

void CPU::Step()
{
    DoOpcode();
    FlushFlags();
}

struct CPU::Operations {

    enum RMType {
        Addr,
        AddrSS,
        Reg
    };

    struct ModRM {
        uint16_t addr;
        uint8_t type;
        uint8_t reg;
    };

    static ModRM GetModRM(CPU* cpu)
    {
        auto modRM = cpu->ReadByte(CS, cpu->state.ip++);
        ModRM result;
        result.reg = (modRM >> 3) & 7;
        if ((modRM & 0xC0) == 0xC0) {
            result.addr = modRM & 7;
            result.type = Reg;
            return result;
        }
        result.type = Addr;
        auto regs = cpu->state.gpr;
        switch (modRM & 7) {
        case 0:
            result.addr = regs[BX] + regs[SI];
            break;
        case 1:
            result.addr = regs[BX] + regs[DI];
            break;
        case 2:
            result.type = AddrSS;
            result.addr = regs[BP] + regs[SI];
            break;
        case 3:
            result.type = AddrSS;
            result.addr = regs[BP] + regs[DI];
            break;
        case 4:
            result.addr = regs[SI];
            break;
        case 5:
            result.addr = regs[DI];
            break;
        case 6:
            if ((modRM & 0xC0) == 0) {
                result.addr = 0;
                modRM |= 0x80;
                break;
            }
            result.type = AddrSS;
            result.addr = regs[BP];
            break;
        case 7:
            result.addr = regs[BX];
            break;
        }
        switch (modRM & 0xC0) {
        case 0:
            break;
        case 0x40:
            result.addr += cpu->ReadByte(CS, cpu->state.ip++);
            break;
        case 0x80:
            result.addr += cpu->ReadWord(CS, cpu->state.ip);
            cpu->state.ip += 2;
            break;
        }
        return result;
    }

    static SegmentRegister GetSeg(Prefixes prefixes, int type)
    {
        if (prefixes.segment == SegReserve) {
            return SegmentRegister(SS + (type != AddrSS));
        }
        return SegmentRegister(prefixes.segment);
    }

    static void ReadRMB(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache)
    {
        auto regs = cpu->state.gpr;
        if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cache.a = cpu->ReadByte(sreg, modrm.addr) * 0x100;
        } else {
            cache.a = (regs[modrm.addr & 3] << ((modrm.addr * 2 & 8) ^ 8)) & 0xFF00;
        }
        cache.b = (regs[modrm.reg & 3] << ((modrm.reg * 2 & 8) ^ 8)) & 0xFF00;
    }

    static void ReadRM(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache)
    {
        auto regs = cpu->state.gpr;
        if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cache.a = cpu->ReadWord(sreg, modrm.addr);
        } else {
            cache.a = regs[modrm.addr];
        }
        cache.b = regs[modrm.reg];
    }

    static void WriteRMB(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache, bool reg)
    {
        auto regs = cpu->state.gpr;
        if (reg) {
            regs[modrm.reg & 3] &= 0xFF00 >> (modrm.reg * 2 & 8);
            regs[modrm.reg & 3] |= cache.r >> ((modrm.reg * 2 & 8) ^ 8);
        } else if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cpu->WriteByte(sreg, modrm.addr, cache.r >> 8);
        } else {
            regs[modrm.addr & 3] &= 0xFF00 >> (modrm.addr * 2 & 8);
            regs[modrm.addr & 3] |= cache.r >> ((modrm.addr * 2 & 8) ^ 8);
        }
    }

    static void WriteRM(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache, bool reg)
    {
        auto regs = cpu->state.gpr;
        if (reg) {
            regs[modrm.reg] = cache.r;
        } else if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cpu->WriteWord(sreg, modrm.addr, cache.r);
        } else {
            regs[modrm.addr] = cache.r;
        }
    }

    static void AddB(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        ModRM operands = GetModRM(cpu);
        FlagsCache cache;
        cache.type = cache.Arithmetic;
        ReadRMB(cpu, prefixes, operands, cache);
        cache.r = cache.a + cache.b;
        WriteRMB(cpu, prefixes, operands, cache, op & 2);
        cache.a = (cache.a >> 8) * 0x101;
        cache.b = (cache.b >> 8) * 0x101;
        cache.r = (cache.r >> 8) * 0x101;
        cpu->flagsCache = cache;
    }

    static void AddW(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        ModRM operands = GetModRM(cpu);
        FlagsCache cache;
        cache.type = cache.Arithmetic;
        ReadRM(cpu, prefixes, operands, cache);
        cache.r = cache.a + cache.b;
        WriteRM(cpu, prefixes, operands, cache, op & 2);
        cpu->flagsCache = cache;
    }

    using Op = void(CPU*, Prefixes, uint8_t op);
    static Op* map1[256];
};

CPU::Operations::Op* CPU::Operations::map1[256] = {
    AddB, AddW, AddB, AddW
};

int CPU::DoOpcode()
{
    auto prevIP = state.ip;
    Prefixes prefixes = ParsePrefixes();
    auto op = ReadByte(CS, state.ip++);
    Operations::map1[op](this, prefixes, op);
    return Normal;
}

auto CPU::ParsePrefixes() -> Prefixes
{
    Prefixes prefixes = { SegReserve, 0 };
    auto op = ReadByte(CS, state.ip);
    while (true) {
        switch (op) {
        case 0x26:
            prefixes.segment = ES;
            break;
        case 0x2e:
            prefixes.segment = CS;
            break;
        case 0x36:
            prefixes.segment = SS;
            break;
        case 0x3e:
            prefixes.segment = DS;
            break;
        case 0x64:
            prefixes.segment = FS;
            break;
        case 0x65:
            prefixes.segment = GS;
            break;
        case 0x66:
            prefixes.prefix |= P66;
            break;
        case 0x67:
            prefixes.prefix |= P67;
            break;
        case 0xF0:
            prefixes.prefix |= PF0;
            break;
        case 0xF2:
            prefixes.prefix |= PF2;
            break;
        case 0xF3:
            prefixes.prefix |= PF3;
            break;
        default:
            return prefixes;
        }
        ++state.ip;
    }
}

auto CPU::ReadByte(SegmentRegister sreg, uint16_t addr) -> uint8_t
{
    unsigned char byte;
    hook->ReadMem(state, &byte, sizeof(byte), CalcAddr(sreg, addr));
    return byte;
}

uint16_t CPU::ReadWord(SegmentRegister sreg, uint16_t addr)
{
    unsigned char word[2];
    hook->ReadMem(state, word, sizeof(word), CalcAddr(sreg, addr));
    return word[1] * 0x100 + word[0];
}

void CPU::WriteByte(SegmentRegister sreg, uint16_t addr, uint8_t val)
{
    unsigned char byte = val;
    hook->WriteMem(state, CalcAddr(sreg, addr), &byte, sizeof(byte));
}

void CPU::WriteWord(SegmentRegister sreg, uint16_t addr, uint16_t val)
{
    unsigned char word[2] = { static_cast<unsigned char>(val & 0xFF), static_cast<unsigned char>(val >> 8) };
    hook->WriteMem(state, CalcAddr(sreg, addr), &word, sizeof(word));
}

auto CPU::CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t
{
    return addr + state.sregs[sreg] * 0x10;
}

void CPU::FlushFlags()
{
    enum Flags {
        CF = 1,
        PF = 4,
        AF = 16,
        ZF = 64,
        SF = 128,
        OF = 0x800,
        FlagsMask = CF | PF | AF | ZF | SF | OF
    };
    auto& cache = flagsCache;
    auto carry = cache.r < cache.a;
    auto parity = GetParity(cache.r);
    auto acarry = !!((cache.a ^ cache.b ^ cache.r) & 0x10);
    auto zero = !cache.r;
    auto sign = cache.r >= 0x8000;
    bool ovfl = ((cache.a ^ cache.b) >> 15) ^ sign ^ carry;
    state.flags ^= state.flags & FlagsMask;
    state.flags |=
        (CF * carry | OF * ovfl) * (cache.type == cache.Arithmetic) |
        PF * parity | AF * acarry | ZF * zero | SF * sign;
}

} // namespace x86emu
