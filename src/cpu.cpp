#include "include/x86emu/cpu.h"

namespace x86emu {

namespace {

enum Prefix {
    PF0 = 1,
    PF2 = 2,
    PF3 = 3
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


constexpr auto RegSize = sizeof(RegVal);
constexpr auto RegBits = 16;

auto SignExtend(RegVal in, int logSz) -> RegVal
{
    auto smask = 0x80 << (logSz * 8);
    auto mask = 2 * smask - 1;
    return ((in & mask) ^ smask) - smask;
}

enum Flags {
    CF = 1,
    PF = 4,
    AF = 16,
    ZF = 64,
    SF = 128,
    OF = 0x800,
    FlagsMask = CF | PF | AF | ZF | SF | OF
};

}

CPU::CPU(IIOHook& hook) :
    CPU({.ip = 0, .sregs = {0, 0xFFFF}}, hook)
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
    unsigned grp1:2;
    unsigned segment:3;
    unsigned grp3:1;
    unsigned grp4:1;
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
        RegVal addr;
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

    static SegmentRegister GetSeg(Prefixes prefixes, int type = 0)
    {
        if (prefixes.segment == SegReserve) {
            return SegmentRegister(SS + (type != AddrSS));
        }
        return SegmentRegister(prefixes.segment);
    }

    static void ReadRM(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache)
    {
        auto regs = cpu->state.gpr;
        if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cache.ops[0] = cpu->ReadMem(sreg, modrm.addr, cache.opsz);
        } else {
            auto rh = (cache.opsz == 0) * (modrm.addr & 4);
            auto shift = 2 * rh;
            cache.ops[0] = SignExtend(regs[modrm.addr ^ rh] >> shift, cache.opsz);
        }
        auto rh = (cache.opsz == 0) * (modrm.reg & 4);
        auto shift = 2 * rh;
        cache.ops[1] = SignExtend(regs[modrm.reg ^ rh] >> shift, cache.opsz);
    }

    static void WriteRM(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache, bool reg)
    {
        auto regs = cpu->state.gpr;
        auto mask = (0x100 << (cache.opsz * 8)) - 1;
        cache.r = SignExtend(cache.r, cache.opsz);
        if (reg) {
            auto rh = (cache.opsz == 0) * (modrm.reg & 4);
            auto shift = 2 * rh;
            mask <<= shift;
            auto& r = regs[modrm.reg ^ rh];
            r ^= (r & mask) ^ ((cache.r << shift) & mask);
        } else if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cpu->WriteMem(sreg, modrm.addr, cache.opsz, cache.r);
        } else {
            auto rh = (cache.opsz == 0) * (modrm.reg & 4);
            auto shift = 2 * rh;
            mask <<= shift;
            auto& r = regs[modrm.addr ^ rh];
            r ^= (r & mask) ^ ((cache.r << shift) & mask);
        }
    }

    static void DoBinOp(RegVal a, RegVal b)
    {}

    static void MainBinOp(CPU* cpu, FlagsCache& cache, uint8_t op)
    {
        enum Ops { Add, Or, Adc, Sbb, And, Sub, Xor, Cmp };
        cache.type = cache.Arithmetic;
        bool inverse = !(op & 6);
        switch ((op >> 3) & 7) {
        case Add:
            cache.r = cache.ops[0] + cache.ops[1];
            break;
        case Or:
            cache.type = cache.Logical;
            cache.r = cache.ops[0] | cache.ops[1];
            break;
        case Adc:
            cache.r = cache.ops[0] + cache.ops[1];
            break;
        case Sbb:
            cache.ops[!inverse] = ~cache.ops[!inverse];
            cache.r = ~(cache.ops[0] + cache.ops[1]);
            break;
        case And:
            cache.type = cache.Logical;
            cache.r = cache.ops[0] & cache.ops[1];
            break;
        case Sub:
            cache.ops[!inverse] = ~cache.ops[!inverse];
            cache.r = ~(cache.ops[0] + cache.ops[1]);
            break;
        case Xor:
            cache.type = cache.Logical;
            cache.r = cache.ops[0] ^ cache.ops[1];
            break;
        case Cmp:
            cache.r = cache.ops[inverse];
            break;
        }
    }

    static void BiOp(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        FlagsCache cache;
        cache.opsz = op & 1;
        ReadRM(cpu, prefixes, modRM, cache);
        MainBinOp(cpu, cache, op);
        WriteRM(cpu, prefixes, modRM, cache, op & 2);
        cpu->flagsCache = cache;
    }

    static void BiOpAI(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        prefixes.segment = CS;
        ModRM modRM = {.type = Addr, .reg = AX};
        FlagsCache cache;
        cache.opsz = op & 1;
        modRM.addr = cpu->state.ip,
        cpu->state.ip += 1 << cache.opsz;
        ReadRM(cpu, prefixes, modRM, cache);
        MainBinOp(cpu, cache, op);
        WriteRM(cpu, prefixes, modRM, cache, true);
        cpu->flagsCache = cache;
    }

    static void PushSReg(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        auto logSz = 1;
        RegVal val = cpu->state.sregs[(op >> 3) & 3];
        cpu->WriteMem(SS, cpu->state.gpr[SP] -= 1 << logSz, logSz, val);
    }

    static void PopSReg(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        auto logSz = 1;
        RegVal val = cpu->ReadMem(SS, cpu->state.gpr[SP], logSz);
        cpu->state.sregs[(op >> 3) & 3] = val;
        cpu->state.gpr[SP] += 1 << logSz;
    }

    static void Nop(CPU*, Prefixes, uint8_t)
    {}

    static void AAA(CPU* cpu, Prefixes, uint8_t)
    {
        cpu->FlushFlags();
        if ((cpu->state.gpr[AX] & 0xF) > 9 || (cpu->state.flags & AF)) {
            cpu->state.gpr[AX] += 0x106;
            cpu->state.flags |= AF | CF;
        } else {
            cpu->state.flags ^= cpu->state.flags & (AF | CF);
        }
        cpu->state.gpr[AX] ^= cpu->state.gpr[AX] & 0xF0;
    }

    static void AAS(CPU* cpu, Prefixes, uint8_t)
    {
        cpu->FlushFlags();
        auto regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        if ((regs[AX] & 0xF) > 9 || (flags & AF)) {
            auto tmp = regs[AX] & 0xFF;
            regs[AX] = (regs[AX] ^ tmp) | ((tmp - 6) & 0xFF);
            regs[AX] -= 0x100;
            flags |= AF | CF;
        } else {
            flags ^= flags & (AF | CF);
        }
        regs[AX] ^= regs[AX] & 0xF0;
    }

    static void DAA(CPU* cpu, Prefixes, uint8_t)
    {
        cpu->FlushFlags();
        auto regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        auto tmp = regs[AX] & 0xFF;
        bool a = (flags & AF) || (tmp & 0xF) > 9;
        bool c = (flags & CF) || (tmp > 0x99);
        auto add = a * 6 + c * 0x60;
        tmp += add;
        regs[AX] ^= regs[AX] & 0xFF;
        regs[AX] |= tmp & 0xFF;
        flags ^= flags & (AF | CF);
        flags |= AF * a | CF * c;
    }

    static void DAS(CPU* cpu, Prefixes, uint8_t)
    {
        cpu->FlushFlags();
        auto regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        auto tmp = regs[AX] & 0xFF;
        bool c = (tmp > 0x99) || (flags & CF);
        bool c2 = false;
        bool a = (flags & AF) || (tmp & 0xF) > 9;
        if (a) {
            c2 = (tmp < 6) || (flags & CF);
            tmp -= 6;
        }
        if (c) {
            tmp -= 0x60;
            c2 = true;
        }
        regs[AX] ^= regs[AX] & 0xFF;
        regs[AX] |= tmp & 0xFF;
        flags ^= flags & (AF | CF);
        flags |= AF * a | CF * c2;
    }

    static void Inc(CPU* cpu, Prefixes, uint8_t op)
    {
        cpu->FlushFlags();
        auto regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        bool oldCF = (flags & CF);
        FlagsCache cache;
        cache.type = cache.Arithmetic;
        cache.opsz = 1;
        cache.ops[0] = regs[op & 7];
        cache.ops[1] = 1;
        cache.r = cache.ops[0] + 1;
        regs[op & 7] = cache.r;
        cpu->flagsCache = cache;
        cpu->FlushFlags();
        flags ^= flags & CF;
        flags |= CF * oldCF;
    }

    static void Dec(CPU* cpu, Prefixes, uint8_t op)
    {
        cpu->FlushFlags();
        auto regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        bool oldCF = (flags & CF);
        FlagsCache cache;
        cache.type = cache.Arithmetic;
        cache.opsz = 1;
        cache.ops[0] = ~regs[op & 7];
        cache.ops[1] = 1;
        cache.r = ~(cache.ops[0] + 1);
        regs[op & 7] = cache.r;
        cpu->flagsCache = cache;
        cpu->FlushFlags();
        flags ^= flags & CF;
        flags |= CF * oldCF;
    }

    using Op = void(CPU*, Prefixes, uint8_t op);
    static Op* map1[256];
};

CPU::Operations::Op* CPU::Operations::map1[256] = {
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, Nop,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, DAA,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, DAS,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, AAA,
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, AAS,
    Inc, Inc, Inc, Inc, Inc, Inc, Inc, Inc,
    Dec, Dec, Dec, Dec, Dec, Dec, Dec, Dec,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    Nop, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
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
    Prefixes prefixes = { 0, SegReserve };
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
            prefixes.grp3 = 1;
            break;
        case 0x67:
            prefixes.grp4 = 1;
            break;
        case 0xF0:
            prefixes.grp1 = PF0;
            break;
        case 0xF2:
            prefixes.grp1 = PF2;
            break;
        case 0xF3:
            prefixes.grp1 = PF3;
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

auto CPU::ReadMem(SegmentRegister sreg, uint16_t addr, int logSz) -> RegVal
{
    RegVal result;
    switch(logSz) {
    case 0:
        result = ReadByte(sreg, addr);
        break;
    case 1:
        result = ReadWord(sreg, addr);
        break;
    }
    return SignExtend(result, logSz);
}

void CPU::WriteByte(SegmentRegister sreg, uint16_t addr, uint8_t val)
{
    unsigned char byte = val;
    hook->WriteMem(state, CalcAddr(sreg, addr), &byte, sizeof(byte));
}

void CPU::WriteWord(SegmentRegister sreg, uint16_t addr, uint16_t val)
{
    unsigned char word[2];
    for (int i = 0; i < sizeof(val); ++i) {
        word[i] = val;
        val >>= 8;
    }
    hook->WriteMem(state, CalcAddr(sreg, addr), &word, sizeof(word));
}

void CPU::WriteMem(SegmentRegister sreg, uint16_t addr, int logSz, RegVal val)
{
    switch(logSz) {
    case 0:
        WriteByte(sreg, addr, val);
        break;
    case 1:
        WriteWord(sreg, addr, val);
        break;
    }
}

auto CPU::CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t
{
    return addr + state.sregs[sreg] * 0x10;
}

namespace {

bool GetCF(FlagsCache& cache)
{
    return (cache.type == cache.Arithmetic) && RegVal(~cache.ops[0]) < cache.ops[1];
}

bool GetAF(FlagsCache& cache)
{
    return (~cache.ops[0] & 0xf) < (cache.ops[1] & 0xf);
}

bool GetParity(RegVal val)
{
    val &= 0xFF;
    val = (val & 0xF) ^ ((val & 0xF0) >> 4);
    val = (val & 0x3) ^ ((val & 0xC) >> 2);
    return (val & 0x1) ^ ((val & 0x2) >> 1) ^ 1;
}

bool GetSign(RegVal val)
{
    return val >> (RegBits - 1);
}

bool GetOverflow(FlagsCache& cache)
{
    auto mask = (0x80 << ((8 << cache.opsz) - 8)) - 1;
    return (cache.type == cache.Arithmetic) &&
        (((~cache.ops[0] & mask) < (cache.ops[1] & mask)) ^ GetCF(cache));
}

}

void CPU::FlushFlags()
{
    auto& cache = flagsCache;
    if (cache.type == cache.None) {
        return;
    }
    auto carry = GetCF(cache);
    auto parity = GetParity(cache.r);
    auto acarry = GetAF(cache);
    auto zero = !cache.r;
    auto sign = GetSign(cache.r);
    auto ovfl = GetOverflow(cache);
    state.flags ^= state.flags & FlagsMask;
    state.flags |=
        CF * carry | PF * parity | AF * acarry |
        ZF * zero | SF * sign | OF * ovfl;
    cache.type = cache.None;
}

} // namespace x86emu
