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

bool GetParity(uint16_t val)
{
    val &= 0xFF;
    val = (val & 0xF) ^ ((val & 0xF0) >> 4);
    val = (val & 0x3) ^ ((val & 0xC) >> 2);
    return (val & 0x1) ^ ((val & 0x2) >> 1) ^ 1;
}

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

bool GetCF(FlagsCache& cache)
{
    return cache.r < cache.ops[0] || cache.r < cache.ops[1];
}

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

    static SegmentRegister GetSeg(Prefixes prefixes, int type)
    {
        if (prefixes.segment == SegReserve) {
            return SegmentRegister(SS + (type != AddrSS));
        }
        return SegmentRegister(prefixes.segment);
    }

    static void ReadRM(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache, int logSz)
    {
        auto regs = cpu->state.gpr;
        if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cache.ops[0] = cpu->ReadMem(sreg, modrm.addr, logSz);
        } else {
            auto rh = (logSz == 0) * (modrm.addr & 4);
            auto shift = 2 * rh;
            cache.ops[0] = SignExtend(regs[modrm.addr ^ rh] >> shift, logSz);
        }
        auto rh = (logSz == 0) * (modrm.reg & 4);
        auto shift = 2 * rh;
        cache.ops[1] = SignExtend(regs[modrm.reg ^ rh] >> shift, logSz);
    }

    static void WriteRM(CPU* cpu, Prefixes prefixes, ModRM modrm, FlagsCache& cache, int logSz, bool reg)
    {
        auto regs = cpu->state.gpr;
        auto mask = (0x100 << (logSz * 8)) - 1;
        cache.r = SignExtend(cache.r, logSz);
        if (reg) {
            auto rh = (logSz == 0) * (modrm.reg & 4);
            auto shift = 2 * rh;
            mask <<= shift;
            auto& r = regs[modrm.reg ^ rh];
            r ^= (r & mask) ^ ((cache.r << shift) & mask);
        } else if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cpu->WriteMem(sreg, modrm.addr, logSz, cache.r);
        } else {
            auto rh = (logSz == 0) * (modrm.reg & 4);
            auto shift = 2 * rh;
            mask <<= shift;
            auto& r = regs[modrm.addr ^ rh];
            r ^= (r & mask) ^ ((cache.r << shift) & mask);
        }
    }

    static void MainBinOp(CPU* cpu, FlagsCache& cache, uint8_t op)
    {
        enum Ops { Add, Or, Adc, Sbb, And, Sub, Xor, Cmp };
        cache.type = cache.Arithmetic;
        auto carry = GetCF(cpu->flagsCache);
        switch (op >> 3) {
        case Add:
            cache.r = cache.ops[0] + cache.ops[1];
            break;
        case Or:
            cache.type = cache.Logical;
            cache.r = cache.ops[0] | cache.ops[1];
            break;
        case Adc:
            cache.r = cache.ops[0] + cache.ops[1] + carry;
            break;
        case Sbb:
            cache.ops[!(op & 2)] = -cache.ops[!(op & 2)];
            cache.r = cache.ops[0] + cache.ops[1];
            break;
        case And:
            cache.type = cache.Logical;
            cache.r = cache.ops[0] & cache.ops[1];
            break;
        case Sub:
            cache.ops[!(op & 2)] = -cache.ops[!(op & 2)];
            cache.r = cache.ops[0] + cache.ops[1] - carry;
            break;
        case Xor:
            cache.type = cache.Logical;
            cache.r = cache.ops[0] ^ cache.ops[1];
            break;
        case Cmp:
            break;
        }
    }

    static void Add(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        FlagsCache cache;
        int logSz = op & 1;
        ReadRM(cpu, prefixes, modRM, cache, logSz);
        cache.type = cache.Arithmetic;
        cache.r = cache.ops[0] + cache.ops[1];
        WriteRM(cpu, prefixes, modRM, cache, logSz, op & 2);
        cpu->flagsCache = cache;
    }

    static void AddAccImm(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        prefixes.segment = CS;
        ModRM modRM = {.type = Addr, .reg = AX};
        FlagsCache cache;
        int logSz = op & 1;
        modRM.addr = cpu->state.ip,
        cpu->state.ip += 1 << logSz;
        ReadRM(cpu, prefixes, modRM, cache, logSz);
        cache.type = cache.Arithmetic;
        cache.r = cache.ops[0] + cache.ops[1];
        WriteRM(cpu, prefixes, modRM, cache, logSz, true);
        cpu->flagsCache = cache;
    }

    using Op = void(CPU*, Prefixes, uint8_t op);
    static Op* map1[256];
};

CPU::Operations::Op* CPU::Operations::map1[256] = {
    Add, Add, Add, Add, AddAccImm, AddAccImm
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

void CPU::FlushFlags()
{
    auto& cache = flagsCache;
    auto carry = GetCF(cache);
    auto parity = GetParity(cache.r);
    auto acarry = !!((cache.ops[0] ^ cache.ops[1] ^ cache.r) & 0x10);
    auto zero = !cache.r;
    bool sign = cache.r >> (RegBits - 1);
    bool ovfl = ((cache.ops[0] ^ cache.ops[1]) >> (RegBits - 1)) ^ sign ^ carry;
    state.flags ^= state.flags & FlagsMask;
    state.flags |=
        (CF * carry | OF * ovfl) * (cache.type == cache.Arithmetic) |
        PF * parity | AF * acarry | ZF * zero | SF * sign;
}

} // namespace x86emu
