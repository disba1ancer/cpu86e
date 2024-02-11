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
    auto smask = 0x80 << ((8 << logSz) - 8);
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
}

struct CPU::Calc {
    RegVal n[2];
    RegVal result;
    RegVal flags = 0;
    RegVal flagsMask = FlagsMask;
    int logSz;

    Calc(int logSz) :
        logSz(logSz)
    {}

    auto GetFlags(RegVal f) -> RegVal
    {
        return f ^ ((f ^ flags) & flagsMask);
    }

    static bool GetParity(RegVal val)
    {
        val &= 0xFF;
        val = (val & 0xF) ^ ((val & 0xF0) >> 4);
        val = (val & 0x3) ^ ((val & 0xC) >> 2);
        return (val & 0x1) ^ ((val & 0x2) >> 1) ^ 1;
    }

    static bool GetSign(RegVal val)
    {
        return val >> (RegBits - 1);
    }

    void SetResultFlags() {
        flags |= PF * GetParity(result) | ZF * !result | SF * GetSign(result);
    }

    void SetOperandFlags(bool c) {
        RegVal a = ~n[0];
        RegVal b = n[1];
        bool carry = (a < b) || (c && (a == b));
        auto mask = (0x80 << ((8 << logSz) - 8)) - 1;
        a &= mask;
        b &= mask;
        bool ovf = ((a < b) || (c && (a == b))) ^ carry;
        a &= 0xF;
        b &= 0xF;
        bool af = (a < b) || (c && (a == b));
        flags |= carry * CF | ovf * OF | af * AF;
    }

    enum Op { Add, Or, Adc, Sbb, And, Sub, Xor, Cmp };

    Op DoOp(uint8_t op_, bool cf = false)
    {
        bool inverse = !!(op_ & 6);
        auto op = Op((op_ >> 3) & 7);
        DoOp(op, inverse, cf);
        return op;
    }

    void DoOp(Op op, bool inverse = false, bool cf = false)
    {
        switch (op) {
        case Add:
            result = n[0] + n[1];
            SetOperandFlags(false);
            break;
        case Or:
            result = n[0] | n[1];
            break;
        case Adc:
            result = n[0] + n[1] + cf;
            SetOperandFlags(cf);
            break;
        case Sbb:
            n[inverse] = ~n[inverse];
            result = ~(n[0] + n[1] + cf);
            SetOperandFlags(cf);
            break;
        case And:
            result = n[0] & n[1];
            break;
        case Cmp:
        case Sub:
            n[inverse] = ~n[inverse];
            result = ~(n[0] + n[1]);
            SetOperandFlags(false);
            break;
        case Xor:
            result = n[0] ^ n[1];
            break;
        }
        SetResultFlags();
    }
};

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

    static void ReadRM(CPU* cpu, Prefixes prefixes, ModRM modrm, Calc& calc)
    {
        auto regs = cpu->state.gpr;
        if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            calc.n[0] = cpu->ReadMem(sreg, modrm.addr, calc.logSz);
        } else {
            auto rh = (calc.logSz == 0) * (modrm.addr & 4);
            auto shift = 2 * rh;
            calc.n[0] = SignExtend(regs[modrm.addr ^ rh] >> shift, calc.logSz);
        }
        auto rh = (calc.logSz == 0) * (modrm.reg & 4);
        auto shift = 2 * rh;
        calc.n[1] = SignExtend(regs[modrm.reg ^ rh] >> shift, calc.logSz);
    }

    static void WriteRM(CPU* cpu, Prefixes prefixes, ModRM modrm, Calc& cache, bool reg)
    {
        auto regs = cpu->state.gpr;
        auto mask = (0x100 << (cache.logSz * 8)) - 1;
        if (reg) {
            auto rh = (cache.logSz == 0) * (modrm.reg & 4);
            auto shift = 2 * rh;
            mask <<= shift;
            auto& r = regs[modrm.reg ^ rh];
            r ^= (r & mask) ^ ((cache.result << shift) & mask);
        } else if (modrm.type != Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cpu->WriteMem(sreg, modrm.addr, cache.logSz, cache.result);
        } else {
            auto rh = (cache.logSz == 0) * (modrm.reg & 4);
            auto shift = 2 * rh;
            mask <<= shift;
            auto& r = regs[modrm.addr ^ rh];
            r ^= (r & mask) ^ ((cache.result << shift) & mask);
        }
    }

    static void BiOp(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        auto& flags =cpu->state.flags;
        Calc calc(op & 1);
        ReadRM(cpu, prefixes, modRM, calc);
        if (calc.DoOp(op, flags & CF) != calc.Cmp) {
            WriteRM(cpu, prefixes, modRM, calc, op & 2);
        }
        flags = calc.GetFlags(flags);
    }

    static void BiOpAI(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        prefixes.segment = CS;
        ModRM modRM = {.type = Addr, .reg = AX};
        auto& flags =cpu->state.flags;
        Calc calc(op & 1);
        modRM.addr = cpu->state.ip,
        cpu->state.ip += 1 << calc.logSz;
        ReadRM(cpu, prefixes, modRM, calc);
        calc.DoOp(op, flags & CF);
        if (calc.DoOp(op, flags & CF) != calc.Cmp) {
            WriteRM(cpu, prefixes, modRM, calc, true);
        }
        flags = calc.GetFlags(flags);
    }

    static void PushVal(CPU* cpu, int logSz, RegVal val)
    {
        cpu->WriteMem(SS, cpu->state.gpr[SP] -= 1 << logSz, logSz, val);
    }

    static auto PopVal(CPU* cpu, int logSz) -> RegVal
    {
        RegVal result = cpu->ReadMem(SS, cpu->state.gpr[SP], logSz);
        cpu->state.gpr[SP] += 1 << logSz;
        return result;
    }

    static void PushSReg(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        PushVal(cpu, 1, cpu->state.sregs[(op >> 3) & 3]);
    }

    static void PopSReg(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        cpu->state.sregs[(op >> 3) & 3] = PopVal(cpu, 1);
    }

    static void Nop(CPU*, Prefixes, uint8_t)
    {}

    static void AAA(CPU* cpu, Prefixes, uint8_t)
    {
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

    static void IncDec(CPU* cpu, Prefixes, uint8_t op)
    {
        auto regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        Calc calc(1);
        calc.n[0] = regs[op & 7];
        calc.n[1] = 1;
        calc.DoOp(op & 8 ? calc.Sub : calc.Add);
        calc.flagsMask ^= CF;
        regs[op & 7] = calc.result;
        flags = calc.GetFlags(flags);
    }

    static void PushReg(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        PushVal(cpu, 1, cpu->state.gpr[op & 3]);
    }

    static void PopReg(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        cpu->state.gpr[op & 3] = PopVal(cpu, 1);
    }

    static void Jcc(CPU* cpu, Prefixes prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& flags = cpu->state.flags;
        auto off = cpu->ReadByte(CS, ip++);
        bool cond;
        switch ((op >> 1) & 0x7) {
        case 0:
            cond = flags & OF;
            break;
        case 1:
            cond = flags & CF;
            break;
        case 2:
            cond = flags & ZF;
            break;
        case 3:
            cond = flags & (CF | ZF);
            break;
        case 4:
            cond = flags & SF;
            break;
        case 5:
            cond = flags & PF;
            break;
        case 6:
            cond = !(flags & OF) != !(flags & SF);
            break;
        case 7:
            cond = !(flags & OF) == !(flags & SF) && (flags & ZF);
            break;
        }
        cond = cond ^ (op & 1);
        ip += off * cond;
    }

    using Op = void(CPU*, Prefixes, uint8_t op);
    static Op* map1[256];
};

CPU::Operations::Op* CPU::Operations::map1[256] = {
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, Nop, // 0
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg, // 8
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg, // 0x10
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg, // 0x18
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, DAA, // 0x20
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, DAS, // 0x28
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, AAA, // 0x30
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, 0, AAS, // 0x38
    IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, // 0x40
    IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, // 0x48
    PushReg, PushReg, PushReg, PushReg, PushReg, PushReg, PushReg, PushReg, // 0x50
    PopReg, PopReg, PopReg, PopReg, PopReg, PopReg, PopReg, PopReg, // 0x58
    Nop, Nop, Nop, Nop, 0, 0, 0, 0, // 0x60
    Nop, Nop, Nop, Nop, Nop, Nop, Nop, Nop, // 0x68
    Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, // 0x70
    Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, // 0x78
    0, 0, 0, 0, 0, 0, 0, 0, // 0x80
    0, 0, 0, 0, 0, 0, 0, 0, // 0x88
    Nop, 0, 0, 0, 0, 0, 0, 0, // 0x90
    0, 0, 0, 0, 0, 0, 0, 0, // 0x98
    0, 0, 0, 0, 0, 0, 0, 0, // 0xA8
    0, 0, 0, 0, 0, 0, 0, 0, // 0xA0
    0, 0, 0, 0, 0, 0, 0, 0, // 0xB8
    0, 0, 0, 0, 0, 0, 0, 0, // 0xB0
    0, 0, 0, 0, 0, 0, 0, 0, // 0xC8
    0, 0, 0, 0, 0, 0, 0, 0, // 0xC0
    0, 0, 0, 0, 0, 0, 0, 0, // 0xD0
    0, 0, 0, 0, 0, 0, 0, 0, // 0xD8
    0, 0, 0, 0, 0, 0, 0, 0, // 0xE0
    0, 0, 0, 0, 0, 0, 0, 0, // 0xE8
    0, 0, 0, 0, 0, 0, 0, 0, // 0xF0
    0, 0, 0, 0, 0, 0, 0, 0, // 0xF8
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

} // namespace x86emu
