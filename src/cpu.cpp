#include "include/cpu86e/cpu.h"
#include <cstring>
#include <exception>

namespace cpu86e {

namespace {

enum Prefix {
    PF0 = 1,
    PF2 = 2,
    PF3 = 3
};

enum DoOpcodeResult {
    Normal,
    Continue,
    Repeat,
    Halt,
};

char8_t map[] = {
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 0, 0, 0, 0,
    0, 1, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 0, 0, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 1, 1,
};

constexpr auto RegSize = sizeof(RegVal);
constexpr auto RegBits = 16;

auto ZeroExtend(RegVal in, int logSz) -> RegVal
{
    auto smask = 2 << ((8 << logSz) - 1);
    auto mask = smask - 1;
    return in & mask;
}

auto SignExtend(RegVal in, int logSz) -> RegVal
{
    auto smask = 1 << ((8 << logSz) - 1);
    auto mask = 2 * smask - 1;
    return ((in & mask) ^ smask) - smask;
}

auto SignExtend64(uint64_t in, int logSz) -> uint64_t
{
    auto smask = 0x80 << ((8 << logSz) - 8);
    auto mask = 2 * smask - 1;
    return ((in & mask) ^ smask) - smask;
}

static bool GetSign(RegVal val, int logSz)
{
    return val >> ((8 << logSz) - 1);
}

enum Flags {
    CF = 1,
    PF = 4,
    AF = 16,
    ZF = 64,
    SF = 128,
    TF = 0x100,
    IF = 0x200,
    DF = 0x400,
    OF = 0x800,
    FlagsMask = CF | PF | AF | ZF | SF | OF
};

struct CPUException : std::exception {
public:
    enum E {
        DE,
        DB,
        NMI,
        BP,
        OF,
        BR,
        UD,
        NM,
        DF,
        MF0,
        TS,
        NP,
        SS,
        GP,
        PF,
        MF,
        AC,
        MC,
        XM,
        VE
    };

    CPUException(E e) :
        exception(e)
    {}

    const char *what() const
    {
        return "CPU Exception";
    }

    auto GetException() -> E
    {
        return exception;
    }
private:
    E exception;
};

}

CPU::CPU(IIOHook& hook) :
    CPU(InitState(), hook)
{
}

CPU::CPU(const CPUState &initState, IIOHook& hook) :
    state(initState),
    hook(&hook),
    oldflags(0),
    nmi(0),
    halt(0),
    intr(NoInterrupt)
{}

void CPU::StoreState(CPUState &initState) const
{
    initState = state;
}

void CPU::LoadState(const CPUState &initState)
{
    oldflags = 0;
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

int CPU::Run(int steps)
{
    while (true) {
        auto r = DoStep();
        if (r == Halt) {
            return 1;
        }
        if (steps != -1 && --steps <= 0) {
            return 0;
        }
    }
}

void CPU::Step()
{
    DoStep();
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

    bool GetSign(RegVal val)
    {
        return ::cpu86e::GetSign(val, logSz);
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
        n[0] = SignExtend(n[0], logSz);
        n[1] = SignExtend(n[1], logSz);
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

    enum Op2 { Rol, Ror, Rcl, Rcr, Shl, Shr, Op2UD, Sar };

    void DoOp2(Op2 op, bool cf = false)
    {
        n[0] = ZeroExtend(n[0], logSz);
        n[1] = n[1] & 4;
        if (n[1] == 0) {
            result = n[0];
            flagsMask ^= flagsMask & (CF | OF | AF);
            SetResultFlags();
            return;
        }
        int maxBits = 1 << (logSz + 3);
        auto mask = 1 << (maxBits - n[1] - 1);
        switch (op) {
        case Rol:
            result = (n[0] << n[1]) | (n[0] >> (maxBits - n[1]));
            flags = result & 1;
            break;
        case Ror:
            result = (n[0] >> n[1]) | (n[0] << (maxBits - n[1]));
            flags = (n[0] >> (n[1] - 1)) & 1;
            break;
        case Rcl:
            result = (n[0] << n[1]) | (cf << (n[1] - 1));
            if (n[1] > 1) {
                result |= (n[0] >> (maxBits - n[1] + 1));
            }
            flags = (n[0] << (n[1] - 1)) & 1;
            break;
        case Rcr:
            result = (n[0] >> n[1]) | (cf << (maxBits - n[1]));
            if (n[1] > 1) {
                result |= (n[0] << (maxBits - n[1] + 1));
            }
            flags = (n[0] >> (n[1] - 1)) & 1;
            break;
        case Shl:
            result = n[0] << n[1];
            flags = (n[0] >> (maxBits - n[1])) & 1;
            break;
        case Shr:
            result = n[0] >> n[1];
            flags = (n[0] >> (n[1] - 1)) & 1;
            break;
        case Op2UD:
            throw CPUException(CPUException::UD);
        case Sar:
            result = n[0] >> n[1];
            result = (result ^ mask) - mask;
            flags = (n[0] >> (n[1] - 1)) & 1;
            break;
        }
        if (n[1] == 1) {
            flags |= OF * !!((n[0] ^ result) & mask);
        }
        SetResultFlags();
    }
};

struct CPU::Operations {

    struct ModRM {
        enum RMType {
            Addr,
            AddrSS,
            Reg
        };
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
            result.type = result.Reg;
            return result;
        }
        result.type = result.Addr;
        auto regs = cpu->state.gpr;
        switch (modRM & 7) {
        case 0:
            result.addr = regs[BX] + regs[SI];
            break;
        case 1:
            result.addr = regs[BX] + regs[DI];
            break;
        case 2:
            result.type = result.AddrSS;
            result.addr = regs[BP] + regs[SI];
            break;
        case 3:
            result.type = result.AddrSS;
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
            result.type = result.AddrSS;
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
            return SegmentRegister(SS + (type != ModRM::AddrSS));
        }
        return SegmentRegister(prefixes.segment);
    }

    static auto ReadReg(CPU* cpu, int reg, int logSz) -> RegVal
    {
        return ReadReg(cpu, Register(reg), logSz);
    }

    static auto ReadReg(CPU* cpu, Register reg, int logSz) -> RegVal
    {
        auto regs = cpu->state.gpr;
        auto rh = (logSz == 0) * (reg & 4);
        auto shift = 2 * rh;
        return ZeroExtend(regs[reg ^ rh] >> shift, logSz);
    }

    static void WriteReg(CPU* cpu, int reg, int logSz, RegVal val)
    {
        return WriteReg(cpu, Register(reg), logSz, val);
    }

    static void WriteReg(CPU* cpu, Register reg, int logSz, RegVal val)
    {
        auto regs = cpu->state.gpr;
        auto mask = (0x100 << ((8 << logSz) - 8)) - 1;
        auto rh = (logSz == 0) * (reg & 4);
        auto shift = 2 * rh;
        mask <<= shift;
        auto& r = regs[reg ^ rh];
        r ^= (r & mask) ^ ((val << shift) & mask);
    }

    static void ReadRM(CPU* cpu, Prefixes prefixes, ModRM modrm, Calc& calc)
    {
        if (modrm.type != modrm.Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            calc.n[0] = cpu->ReadMem(sreg, modrm.addr, calc.logSz);
        } else {
            calc.n[0] = ReadReg(cpu, modrm.addr, calc.logSz);
        }
        calc.n[0] = ReadRM(cpu, prefixes, modrm, calc.logSz);
        calc.n[1] = ReadReg(cpu, modrm.reg, calc.logSz);
    }

    static auto ReadRM(CPU* cpu, Prefixes prefixes, ModRM modrm, int logSz) -> RegVal
    {
        if (modrm.type != modrm.Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            return cpu->ReadMem(sreg, modrm.addr, logSz);
        }
        return ReadReg(cpu, modrm.addr, logSz);
    }

    static void WriteRM(CPU* cpu, Prefixes prefixes, ModRM modrm, Calc& cache, bool reg)
    {
        if (reg) {
            WriteReg(cpu, modrm.reg, cache.logSz,cache.result);
        } else {
            WriteRM(cpu, prefixes, modrm, cache.logSz, cache.result);
        }
    }

    static void WriteRM(CPU* cpu, Prefixes prefixes, ModRM modrm, int logSz, RegVal val)
    {
        if (modrm.type != modrm.Reg) {
            auto sreg = GetSeg(prefixes, modrm.type);
            cpu->WriteMem(sreg, modrm.addr, logSz, val);
        } else {
            WriteReg(cpu, modrm.addr, logSz, val);
        }
    }

    static int BiOp(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        auto& flags =cpu->state.flags;
        Calc calc(op & 1);
        ReadRM(cpu, prefixes, modRM, calc);
        if (calc.DoOp(op, flags & CF) != calc.Cmp) {
            WriteRM(cpu, prefixes, modRM, calc, op & 2);
        }
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int BiOpAI(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        prefixes.segment = CS;
        ModRM modRM = {.type = modRM.Addr, .reg = AX};
        auto& flags = cpu->state.flags;
        Calc calc(op & 1);
        modRM.addr = cpu->state.ip,
        cpu->state.ip += 1 << calc.logSz;
        ReadRM(cpu, prefixes, modRM, calc);
        if (calc.DoOp(op, flags & CF) != calc.Cmp) {
            WriteReg(cpu, AX, calc.logSz, calc.result);
        }
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int BiOpIm(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        auto& flags = cpu->state.flags;
        auto& ip = cpu->state.ip;
        Calc calc(op & 1);
        calc.n[1] = cpu->ReadMem(CS, ip, (op & 3) == 1);
        ip += 1 << ((op & 3) == 1);
        calc.n[0] = ReadRM(cpu, prefixes, modRM, calc.logSz);
        auto oprm = Calc::Op(modRM.reg);
        calc.DoOp(oprm, false, flags & CF);
        if (oprm != calc.Cmp) {
            WriteRM(cpu, prefixes, modRM, calc.logSz, calc.result);
        }
        flags = calc.GetFlags(flags);
        return Normal;
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

    static int PushSReg(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        PushVal(cpu, 1, cpu->state.sregs[(op >> 3) & 3]);
        return Normal;
    }

    static int PopSReg(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.sregs[(op >> 3) & 3] = PopVal(cpu, 1);
        return Normal;
    }

    static int SegOvr(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        prefixes.segment = (op >> 3) & 3;
        return Continue;
    }

    static int Nop(CPU*, Prefixes&, uint8_t)
    {
        return Normal;
    }

    static int Hlt(CPU*, Prefixes&, uint8_t)
    {
        return Halt;
    }

    static int AAA(CPU* cpu, Prefixes&, uint8_t)
    {
        if ((cpu->state.gpr[AX] & 0xF) > 9 || (cpu->state.flags & AF)) {
            cpu->state.gpr[AX] += 0x106;
            cpu->state.flags |= AF | CF;
        } else {
            cpu->state.flags ^= cpu->state.flags & (AF | CF);
        }
        cpu->state.gpr[AX] ^= cpu->state.gpr[AX] & 0xF0;
        return Normal;
    }

    static int AAS(CPU* cpu, Prefixes&, uint8_t)
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
        return Normal;
    }

    static int AAM(CPU* cpu, Prefixes&, uint8_t)
    {
        auto& ip = cpu->state.ip;
        auto& ax = cpu->state.gpr[AX];
        auto& flags = cpu->state.flags;
        auto imm = cpu->ReadByte(CS, ip);
        if (imm == 0) {
            throw CPUException(CPUException::DE);
        }
        auto t = ax & 0xFF;
        ax = (t / imm << 8) | (t % imm);
        Calc calc(0);
        calc.result = ax;
        calc.SetResultFlags();
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int AAD(CPU* cpu, Prefixes&, uint8_t)
    {
        auto& ip = cpu->state.ip;
        auto& ax = cpu->state.gpr[AX];
        auto& flags = cpu->state.flags;
        auto imm = cpu->ReadByte(CS, ip);
        auto t = ax >> 8;
        ax = (ax + t * imm) & 0xFF;
        Calc calc(0);
        calc.result = ax;
        calc.SetResultFlags();
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int DAA(CPU* cpu, Prefixes&, uint8_t)
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
        return Normal;
    }

    static int DAS(CPU* cpu, Prefixes&, uint8_t)
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
        return Normal;
    }

    static int IncDec(CPU* cpu, Prefixes&, uint8_t op)
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
        return Normal;
    }

    static int PushReg(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        PushVal(cpu, 1, cpu->state.gpr[op & 3]);
        return Normal;
    }

    static int PopReg(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.gpr[op & 3] = PopVal(cpu, 1);
        return Normal;
    }

    static int Jcc(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& flags = cpu->state.flags;
        auto off = SignExtend(cpu->ReadByte(CS, ip++), 0);
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
            cond = !(flags & OF) == !(flags & SF) && !(flags & ZF);
            break;
        }
        cond = cond ^ (op & 1);
        ip += off * cond;
        return Normal;
    }

    static int Test(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        auto& flags =cpu->state.flags;
        Calc calc(op & 1);
        ReadRM(cpu, prefixes, modRM, calc);
        calc.DoOp(calc.And, flags & CF);
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int TestAI(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        prefixes.segment = CS;
        ModRM modRM = {.type = modRM.Addr, .reg = AX};
        auto& flags = cpu->state.flags;
        Calc calc(op & 1);
        modRM.addr = cpu->state.ip,
        cpu->state.ip += 1 << calc.logSz;
        ReadRM(cpu, prefixes, modRM, calc);
        calc.DoOp(calc.And, false, flags & CF);
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int Xchg(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        int logSz = op & 1;
        auto temp = ReadReg(cpu, modRM.reg, logSz);
        RegVal tmp2;
        if (modRM.type == modRM.Reg) {
            tmp2 = ReadReg(cpu, modRM.addr, logSz);
            WriteReg(cpu, modRM.addr, logSz, temp);
        } else {
            auto sreg = GetSeg(prefixes, modRM.type);
            tmp2 = cpu->ReadMem(sreg, modRM.addr, logSz);
            cpu->WriteMem(sreg, modRM.addr, logSz, temp);
        }
        WriteReg(cpu, modRM.reg, logSz, tmp2);
        return Normal;
    }

    static int Mov(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        int logSz = op & 1;
        auto temp = ReadReg(cpu, modRM.reg, logSz);
        WriteRM(cpu, prefixes, modRM, logSz, temp);
        return Normal;
    }

    static int MovR(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        int logSz = op & 1;
        auto temp = ReadRM(cpu, prefixes, modRM, logSz);
        WriteReg(cpu, modRM.reg, logSz, temp);
        return Normal;
    }

    static int MovI(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        ModRM modRM = GetModRM(cpu);
        int logSz = op & 1;
        auto temp = cpu->ReadMem(CS, ip, logSz);
        ip += 1 << logSz;
        WriteRM(cpu, prefixes, modRM, logSz, temp);
        return Normal;
    }

    static int MovSreg(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto sreg = cpu->state.sregs;
        ModRM modRM = GetModRM(cpu);
        int logSz = 1;
        RegVal temp = sreg[modRM.reg];
        if (modRM.type == modRM.Reg) {
            WriteReg(cpu, modRM.addr, logSz, temp);
        } else {
            auto sreg = GetSeg(prefixes, modRM.type);
            cpu->WriteMem(sreg, modRM.addr, logSz, (temp));
        }
        return Normal;
    }

    static int MovSregR(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto sreg = cpu->state.sregs;
        ModRM modRM = GetModRM(cpu);
        if (modRM.reg == CS) {
            throw CPUException(CPUException::UD);
        }
        int logSz = 1;
        RegVal temp;
        if (modRM.type == modRM.Reg) {
            temp = ReadReg(cpu, modRM.addr, logSz);
        } else {
            auto sreg = GetSeg(prefixes, modRM.type);
            temp = cpu->ReadMem(sreg, modRM.addr, logSz);
        }
        sreg[modRM.reg] = temp;
        return Normal;
    }

    static int Lea(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        int logSz = 1;
        if (modRM.type == modRM.Reg) {
            throw CPUException(CPUException::UD);
        }
        WriteReg(cpu, modRM.reg, logSz, modRM.addr);
        return Normal;
    }

    static int PopRM(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modRM = GetModRM(cpu);
        int logSz = 1;
        if (modRM.reg) {
            throw CPUException(CPUException::UD);
        }
        if (modRM.type == modRM.Reg) {
            PopReg(cpu, prefixes, modRM.addr);
        } else {
            auto regs = cpu->state.gpr;
            auto sreg = GetSeg(prefixes, modRM.type);
            auto temp = cpu->ReadMem(SS, regs[SP], logSz);
            cpu->WriteMem(sreg, modRM.addr, logSz, temp);
            regs[SP] += 2;
        }
        return Normal;
    }

    static int XchgA(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        int logSz = op & 1;
        auto reg = Register(op & 7);
        auto tmp1 = ReadReg(cpu, AX, logSz);
        auto tmp2 = ReadReg(cpu, reg, logSz);
        WriteReg(cpu, AX, logSz, tmp2);
        WriteReg(cpu, reg, logSz, tmp1);
        return Normal;
    }

    static int Cbw(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        int logSz = 0;
        auto temp = SignExtend(ReadReg(cpu, AX, logSz), logSz);
        WriteReg(cpu, AX, logSz + 1, temp);
        return Normal;
    }

    static int Cwd(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        int logSz = 1;
        auto temp = SignExtend(ReadReg(cpu, AX, logSz), logSz);
        WriteReg(cpu, DX, logSz, -GetSign(temp, logSz));
        return Normal;
    }

    static int CallF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& cs = cpu->state.sregs[CS];
        auto logSz = 1;
        auto off = cpu->ReadMem(CS, ip, logSz);
        ip += 1 << logSz;
        auto seg = cpu->ReadWord(CS, ip);
        ip += 2;
        PushVal(cpu, 1, cs);
        PushVal(cpu, logSz, ip);
        cs = seg;
        ip = off;
        return Normal;
    }

    static int Esc(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        GetModRM(cpu);
        return Normal;
    }

    static int PushF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        int logSz = 1;
        PushVal(cpu, logSz, cpu->state.flags);
        return Normal;
    }

    static int PopF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        int logSz = 1;
        cpu->state.flags = PopVal(cpu, logSz);
        return Normal;
    }

    static int SahF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& flags = cpu->state.flags;
        flags ^= (flags & 0xFF) ^ ReadReg(cpu, SP, 0);
        return Normal;
    }

    static int LahF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& flags = cpu->state.flags;
        WriteReg(cpu, SP, 0, flags);
        return Normal;
    }

    static int MovAxM(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        int logSz = op & 1;
        auto addr = cpu->ReadMem(CS, ip, 1); // TODO: Address size
        ip += 1 << logSz;
        auto seg = GetSeg(prefixes);
        auto temp = cpu->ReadMem(seg, addr, logSz);
        WriteReg(cpu, AX, logSz, temp);
        return Normal;
    }

    static int MovMAx(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ax = cpu->state.gpr[AX];
        auto& ip = cpu->state.ip;
        int logSz = op & 1;
        auto addr = cpu->ReadMem(CS, ip, 1);
        ip += 1 << logSz;
        auto seg = GetSeg(prefixes);
        cpu->WriteMem(seg, addr, logSz, ax);
        return Normal;
    }

    static int Movs(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto counter = ReadReg(cpu, CX, 1);
        if (prefixes.grp1 == PF3 && counter == 0) {
            return Normal;
        }
        auto& regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        int logSz = op & 1;
        auto seg = GetSeg(prefixes);
        auto temp = cpu->ReadMem(seg, regs[SI], logSz);
        cpu->WriteMem(ES, regs[DI], logSz, temp);
        int size = (1 << logSz) * (2 * !(flags & DF) - 1);
        regs[SI] += size;
        regs[DI] += size;
        if (prefixes.grp1 == PF3) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0) * Repeat;
        }
        return Normal;
    }

    static int Cmps(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto counter = ReadReg(cpu, CX, 1);
        if ((prefixes.grp1 == PF3 || prefixes.grp1 == PF2) && counter == 0) {
            return Normal;
        }
        auto& regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        int logSz = op & 1;
        auto seg = GetSeg(prefixes);
        Calc calc(logSz);
        calc.n[0] = cpu->ReadMem(seg, regs[SI], logSz);
        calc.n[1] = cpu->ReadMem(ES, regs[DI], logSz);
        calc.DoOp(calc.Cmp);
        flags = calc.GetFlags(flags);
        int size = (1 << logSz) * (2 * !(flags & DF) - 1);
        regs[SI] += size;
        regs[DI] += size;
        if (prefixes.grp1 == PF3) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0 || (flags & ZF)) * Repeat;
        } else if (prefixes.grp1 == PF2) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0 || !(flags & ZF)) * Repeat;
        }
        return Normal;
    }

    static int Stos(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto counter = ReadReg(cpu, CX, 1);
        if (prefixes.grp1 == PF3 && counter == 0) {
            return Normal;
        }
        auto& regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        int logSz = op & 1;
        cpu->WriteMem(ES, regs[DI], logSz, regs[AX]);
        int size = (1 << logSz) * (2 * !(flags & DF) - 1);
        regs[DI] += size;
        if (prefixes.grp1 == PF3) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0) * Repeat;
        }
        return Normal;
    }

    static int Lods(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto counter = ReadReg(cpu, CX, 1);
        if (prefixes.grp1 == PF3 && counter == 0) {
            return Normal;
        }
        auto& regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        int logSz = op & 1;
        auto seg = GetSeg(prefixes);
        auto temp = cpu->ReadMem(seg, regs[SI], logSz);
        WriteReg(cpu, AX, logSz, temp);
        int size = (1 << logSz) * (2 * !(flags & DF) - 1);
        regs[SI] += size;
        if (prefixes.grp1 == PF3) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0) * Repeat;
        }
        return Normal;
    }

    static int Scas(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto counter = ReadReg(cpu, CX, 1);
        if ((prefixes.grp1 == PF3 || prefixes.grp1 == PF2) && counter == 0) {
            return Normal;
        }
        auto& regs = cpu->state.gpr;
        auto& flags = cpu->state.flags;
        int logSz = op & 1;
        Calc calc(logSz);
        calc.n[0] = regs[AX];
        calc.n[1] = cpu->ReadMem(ES, regs[DI], logSz);
        calc.DoOp(calc.Cmp);
        flags = calc.GetFlags(flags);
        int size = (1 << logSz) * (2 * !(flags & DF) - 1);
        regs[DI] += size;
        if (prefixes.grp1 == PF3) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0 || (flags & ZF)) * Repeat;
        } else if (prefixes.grp1 == PF2) {
            counter -= 1;
            WriteReg(cpu, CX, 1, counter);
            return (counter != 0 || !(flags & ZF)) * Repeat;
        }
        return Normal;
    }

    static int MovImm(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        int logSz = !!(op & 8);
        auto& ip = cpu->state.ip;
        RegVal temp = cpu->ReadMem(CS, ip, logSz);
        ip += 1 << logSz;
        WriteReg(cpu, op & 7, logSz, temp);
        return Normal;
    }

    static int ShiftI(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& flags = cpu->state.flags;
        ModRM modrm = GetModRM(cpu);
        Calc calc(op & 1);
        calc.n[1] = cpu->ReadMem(CS, ip, 0);
        ip += 1;
        calc.n[0] = ReadRM(cpu, prefixes, modrm, calc.logSz);
        calc.DoOp2(Calc::Op2(modrm.reg), cpu->state.flags & CF);
        WriteRM(cpu, prefixes, modrm, calc.logSz, calc.result);
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int Shift1(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& flags = cpu->state.flags;
        ModRM modrm = GetModRM(cpu);
        Calc calc(op & 1);
        calc.n[1] = 1;
        ip += 1;
        calc.n[0] = ReadRM(cpu, prefixes, modrm, calc.logSz);
        calc.DoOp2(Calc::Op2(modrm.reg), cpu->state.flags & CF);
        WriteRM(cpu, prefixes, modrm, calc.logSz, calc.result);
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int ShiftC(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& flags = cpu->state.flags;
        ModRM modrm = GetModRM(cpu);
        Calc calc(op & 1);
        calc.n[1] = ReadReg(cpu, CX, 0);
        ip += 1;
        calc.n[0] = ReadRM(cpu, prefixes, modrm, calc.logSz);
        calc.DoOp2(Calc::Op2(modrm.reg), cpu->state.flags & CF);
        WriteRM(cpu, prefixes, modrm, calc.logSz, calc.result);
        flags = calc.GetFlags(flags);
        return Normal;
    }

    static int Ret(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto addr = PopVal(cpu, 1);
        ip = addr;
        return Normal;
    }

    static int RetI(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto imm = cpu->ReadWord(CS, ip);
        auto addr = PopVal(cpu, 1);
        cpu->state.gpr[SP] += imm;
        ip = addr;
        return Normal;
    }

    static int Lxs(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        ModRM modrm = GetModRM(cpu);
        if (modrm.type == modrm.Reg) {
            throw CPUException(CPUException::UD);
        }
        auto ptr = ReadRM(cpu, prefixes, modrm, 1);
        modrm.addr += 2;
        cpu->state.sregs[(op & 1) * 3] = ReadRM(cpu, prefixes, modrm, 1);
        WriteReg(cpu, modrm.reg, 1, ptr);
        return Normal;
    }

    static int RetF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& cs = cpu->state.sregs[CS];
        ip = PopVal(cpu, 1);
        cs = PopVal(cpu, 1);
        return Normal;
    }

    static int RetFI(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& cs = cpu->state.sregs[CS];
        auto imm = cpu->ReadWord(CS, ip);
        cpu->state.gpr[SP] += imm;
        ip = PopVal(cpu, 1);
        cs = PopVal(cpu, 1);
        return Normal;
    }

    static int Int3(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->InitInterrupt(CPUException::BP);
        return Normal;
    }

    static int Int(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto imm = cpu->ReadByte(CS, ip++);
        cpu->InitInterrupt(imm);
        return Normal;
    }

    static int IntO(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        if (cpu->state.flags & OF) {
            cpu->InitInterrupt(CPUException::OF);
        }
        return Normal;
    }

    static int IRet(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& state = cpu->state;
        state.ip = PopVal(cpu, 1);
        state.sregs[CS] = PopVal(cpu, 1);
        state.flags = PopVal(cpu, 1);
        return Normal;
    }

    static int Xlat(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto regs = cpu->state.gpr;
        auto seg = GetSeg(prefixes);
        auto t = cpu->ReadMem(seg, regs[BX], 0);
        WriteReg(cpu, AX, 0, t);
        return Normal;
    }

    static int Loopcc(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& flags = cpu->state.flags;
        auto off = SignExtend(cpu->ReadByte(CS, ip++), 0);
        auto cx = ReadReg(cpu, CX, 1);
        cx--;
        WriteReg(cpu, CX, 1, cx);
        if (cx != 0 && (!(flags & ZF) ^ (op & 1)) + (op & 2)) {
            ip += off;
        }
        return Normal;
    }

    static int Jcxz(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto off = SignExtend(cpu->ReadByte(CS, ip++), 0);
        auto cx = ReadReg(cpu, CX, 1);
        cx--;
        WriteReg(cpu, CX, 1, cx);
        ip += off * (cx == 0);
        return Normal;
    }

    static int In(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        uint16_t port;
        if (op & 8) {
            port = cpu->state.gpr[DX];
        } else {
            port = cpu->ReadByte(CS, ip++);
        }
        auto logSz = op & 1;
        RegVal temp;
        if (logSz) {
            temp = cpu->hook->ReadIOByte(port);
        } else {
            temp = cpu->hook->ReadIOWord(port);
        }
        WriteReg(cpu, AX, logSz, temp);
        return Normal;
    }

    static int Out(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        uint16_t port;
        if (op & 8) {
            port = cpu->state.gpr[DX];
        } else {
            port = cpu->ReadByte(CS, ip++);
        }
        auto logSz = op & 1;
        RegVal temp = ReadReg(cpu, AX, logSz);
        if (!logSz) {
            cpu->hook->WriteIOByte(port, temp);
        } else {
            cpu->hook->WriteIOWord(port, temp);
        }
        return Normal;
    }

    static int Jmp(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto logSz = !(op & 2);
        auto off = SignExtend(cpu->ReadMem(CS, ip, logSz), logSz);
        ip += off + (1 << logSz);
        return Normal;
    }

    static int JmpF(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto& cs = cpu->state.sregs[CS];
        auto logSz = 1;
        auto off = cpu->ReadMem(CS, ip, logSz);
        ip += 1 << logSz;
        auto seg = cpu->ReadWord(CS, ip);
        cs = seg;
        ip = off;
        return Normal;
    }

    static int Call(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        auto& ip = cpu->state.ip;
        auto logSz = 1;
        auto off = SignExtend(cpu->ReadByte(CS, ip), logSz);
        ip += 1 << logSz;
        PushVal(cpu, logSz, ip);
        ip += off;
        return Normal;
    }

    static int Cmc(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags ^= CF;
        return Normal;
    }

    static int Grp3(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        enum Op3 { Test, Op3UD, Not, Neg, Mul, IMul, Div, IDiv };
        auto& flags = cpu->state.flags;
        auto& ip = cpu->state.ip;
        auto modrm = GetModRM(cpu);
        auto logSz = op & 1;
        Calc calc(logSz);
        calc.n[0] = ReadRM(cpu, prefixes, modrm, logSz);
        switch (modrm.reg) {
        case Test:
            calc.n[1] = cpu->ReadMem(CS, ip, logSz);
            calc.DoOp(calc.And);
            flags = calc.GetFlags(flags);
            break;
        case Op3UD:
            throw CPUException(CPUException::UD);
        case Not:
            calc.result = ~calc.n[0];
            calc.flagsMask = 0;
            WriteRM(cpu, prefixes, modrm, logSz, calc.result);
            flags = calc.GetFlags(flags);
            break;
        case Neg:
            calc.n[1] = 0;
            calc.DoOp(calc.Sub, true);
            WriteRM(cpu, prefixes, modrm, logSz, calc.result);
            flags = calc.GetFlags(flags);
            break;
        case Mul: {
            uint64_t t = ReadReg(cpu, AX, logSz);
            t *= calc.n[0];
            if(logSz == 0) {
                WriteReg(cpu, AX, 1, t);
                break;
            }
            WriteReg(cpu, AX, logSz, t);
            t >>= (8 << logSz);
            WriteReg(cpu, DX, logSz, t);
            auto mulFlags = (CF | OF) * (t > 0);
            flags ^= flags & (CF | OF);
            flags ^= mulFlags;
            break;
        }
        case IMul: {
            uint64_t t = ReadReg(cpu, AX, logSz);
            t = SignExtend64(t, logSz);
            t *= SignExtend64(calc.n[0], logSz);
            if(logSz == 0) {
                WriteReg(cpu, AX, 1, t);
                break;
            }
            WriteReg(cpu, AX, logSz, t);
            t >>= (8 << logSz);
            WriteReg(cpu, DX, logSz, t);
            auto mulFlags = (CF | OF) * (t + 1 > 1);
            flags ^= flags & (CF | OF);
            flags ^= mulFlags;
            break;
        }
        case Div: {
            if (logSz != 0) {
                uint64_t t = ReadReg(cpu, DX, logSz);
                if (t >= calc.n[0]) {
                    throw CPUException(CPUException::DE);
                }
                t = (t << (8 << logSz)) + ReadReg(cpu, AX, logSz);
                WriteReg(cpu, AX, logSz, t / calc.n[0]);
                WriteReg(cpu, DX, logSz, t % calc.n[0]);
            } else {
                auto t = ReadReg(cpu, AX, 1);
                if ((t >> 8) >= calc.n[0]) {
                    throw CPUException(CPUException::DE);
                }
                WriteReg(cpu, AX, 0, t / calc.n[0]);
                WriteReg(cpu, SP, 0, t % calc.n[0]);
            }
            break;
        }
        case IDiv: {
            bool sign0 = GetSign(calc.n[0], logSz);
            calc.n[0] = SignExtend(calc.n[0], logSz);
            calc.n[0] *= 1 - 2 * sign0;
            if (logSz != 0) {
                uint64_t t = ReadReg(cpu, DX, logSz);
                auto sign1 = GetSign(t, logSz);
                t <<= 8 << logSz;
                t |= ReadReg(cpu, AX, logSz);
                t = SignExtend64(t, logSz + 1);
                int signMod = 1 - 2 * sign1;
                t *= signMod;
                if ((t >> (8 << logSz)) >= calc.n[0]) {
                    throw CPUException(CPUException::DE);
                }
                int sign = 1 - 2 * (sign0 ^ sign1);
                WriteReg(cpu, AX, logSz, (t / calc.n[0]) * sign);
                WriteReg(cpu, DX, logSz, (t % calc.n[0]) * signMod);
            } else {
                auto t = ReadReg(cpu, AX, 1);
                auto sign1 = GetSign(t, 1);
                t = SignExtend64(t, 1);
                int signMod = 1 - 2 * sign1;
                t *= signMod;
                if ((t >> (8 << logSz)) >= calc.n[0]) {
                    throw CPUException(CPUException::DE);
                }
                int sign = 1 - 2 * (sign0 ^ sign1);
                WriteReg(cpu, AX, 0, (t / calc.n[0]) * sign);
                WriteReg(cpu, SP, 0, (t % calc.n[0]) * signMod);
            }
            break;
        }
        }
        return Normal;
    }

    static int Clc(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags ^= cpu->state.flags & CF;
        return Normal;
    }

    static int Stc(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags |= CF;
        return Normal;
    }

    static int Cli(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags ^= cpu->state.flags & IF;
        return Normal;
    }

    static int Sti(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags |= IF;
        return Normal;
    }

    static int Cld(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags ^= cpu->state.flags & DF;
        return Normal;
    }

    static int Std(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        cpu->state.flags |= DF;
        return Normal;
    }

    static int Grp4(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        enum Op3 { Inc, Dec };
        auto& flags = cpu->state.flags;
        auto modrm = GetModRM(cpu);
        auto logSz = 0;
        Calc calc(logSz);
        calc.flagsMask ^= CF;
        calc.n[0] = ReadRM(cpu, prefixes, modrm, logSz);
        calc.n[1] = 1;
        switch (modrm.reg) {
        case Inc:
            calc.DoOp(calc.Add);
            break;
        case Dec:
            calc.DoOp(calc.Sub);
            break;
        default:
            throw CPUException(CPUException::UD);
            break;
        }
        flags = calc.GetFlags(flags);
        WriteRM(cpu, prefixes, modrm, logSz, calc.result);
        return Normal;
    }

    static int Grp5(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        enum Op3 { Inc, Dec, Call, CallF, Jmp, JmpF, Push };
        auto& flags = cpu->state.flags;
        auto& ip = cpu->state.ip;
        auto modrm = GetModRM(cpu);
        auto logSz = 1;
        Calc calc(logSz);
        calc.flagsMask ^= CF;
        calc.n[1] = 1;
        auto sreg = GetSeg(prefixes);
        switch (modrm.reg) {
        case Inc:
            calc.n[0] = ReadRM(cpu, prefixes, modrm, logSz);
            calc.DoOp(calc.Add);
            flags = calc.GetFlags(flags);
            WriteRM(cpu, prefixes, modrm, logSz, calc.result);
            break;
        case Dec:
            calc.n[0] = ReadRM(cpu, prefixes, modrm, logSz);
            calc.DoOp(calc.Sub);
            flags = calc.GetFlags(flags);
            WriteRM(cpu, prefixes, modrm, logSz, calc.result);
            break;
        case Call:
            PushVal(cpu, 1, ip);
            ip = ReadRM(cpu, prefixes, modrm, 1);
            break;
        case CallF:
            if (modrm.type == modrm.Reg) {
                throw CPUException(CPUException::UD);
            }
            PushVal(cpu, 1, cpu->state.sregs[CS]);
            PushVal(cpu, 1, ip);
            ip = cpu->ReadWord(sreg, modrm.addr);
            modrm.addr += 2;
            cpu->state.sregs[CS] = cpu->ReadWord(sreg, modrm.addr);
            break;
        case Jmp:
            ip = ReadRM(cpu, prefixes, modrm, 1);
            break;
        case JmpF:
            if (modrm.type == modrm.Reg) {
                throw CPUException(CPUException::UD);
            }
            ip = cpu->ReadWord(sreg, modrm.addr);
            modrm.addr += 2;
            cpu->state.sregs[CS] = cpu->ReadWord(sreg, modrm.addr);
            break;
        case Push:
            PushVal(cpu, logSz, ReadRM(cpu, prefixes, modrm, logSz));
            break;
        default:
            throw CPUException(CPUException::UD);
            break;
        }
        return Normal;
    }

    static int Ud(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        throw CPUException(CPUException::UD);
    }

    static int Lock(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        prefixes.grp1 = PF0;
        return Continue;
    }

    static int Rep(CPU* cpu, Prefixes& prefixes, uint8_t op)
    {
        prefixes.grp1 = PF2 + (op & 1);
        return Continue;
    }

    using Op = int(CPU*, Prefixes&, uint8_t op);
    static Op* map1[256];
};

CPU::Operations::Op*
CPU::Operations::map1[256] = {
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg, // 0
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, Ud, // 8
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg, // 0x10
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, PushSReg, PopSReg, // 0x18
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, SegOvr, DAA, // 0x20
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, SegOvr, DAS, // 0x28
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, SegOvr, AAA, // 0x30
    BiOp, BiOp, BiOp, BiOp, BiOpAI, BiOpAI, SegOvr, AAS, // 0x38
    IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, // 0x40
    IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, IncDec, // 0x48
    PushReg, PushReg, PushReg, PushReg, PushReg, PushReg, PushReg, PushReg, // 0x50
    PopReg, PopReg, PopReg, PopReg, PopReg, PopReg, PopReg, PopReg, // 0x58
    Ud, Ud, Ud, Ud, Ud, Ud, Ud, Ud, // 0x60
    Ud, Ud, Ud, Ud, Ud, Ud, Ud, Ud, // 0x68
    Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, // 0x70
    Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, Jcc, // 0x78
    BiOpIm, BiOpIm, BiOpIm, BiOpIm, Test, Test, Xchg, Xchg, // 0x80
    Mov, Mov, MovR, MovR, MovSreg, Lea, MovSregR, PopRM, // 0x88
    Nop, XchgA, XchgA, XchgA, XchgA, XchgA, XchgA, XchgA, // 0x90 // TODO: xchg rax, r8
    Cbw, Cwd, CallF, Nop, PushF, PopF, SahF, LahF, // 0x98
    MovAxM, MovAxM, MovMAx, MovMAx, Movs, Movs, Cmps, Cmps, // 0xA0
    TestAI, TestAI, Stos, Stos, Lods, Lods, Scas, Scas, // 0xA8
    MovImm, MovImm, MovImm, MovImm, MovImm, MovImm, MovImm, MovImm, // 0xB0
    MovImm, MovImm, MovImm, MovImm, MovImm, MovImm, MovImm, MovImm, // 0xB8
    ShiftI, ShiftI, RetI, Ret, Lxs, Lxs, MovI, MovI, // 0xC0
    Ud, Ud, RetFI, RetF, Int3, Int, IntO, IRet, // 0xC8
    Shift1, Shift1, ShiftC, ShiftC, AAM, AAD, Ud, Xlat, // 0xD0
    Esc, Esc, Esc, Esc, Esc, Esc, Esc, Esc, // 0xD8
    Loopcc, Loopcc, Loopcc, Jcxz, In, In, Out, Out, // 0xE0
    Call, Jmp, JmpF, Jmp, In, In, Out, Out, // 0xE8
    Lock, Ud, Rep, Rep, Hlt, Cmc, Grp3, Grp3, // 0xF0
    Clc, Stc, Cli, Sti, Cld, Std, Grp4, Grp5, // 0xF8
};

void CPU::InitInterrupt(int interrupt)
{
    auto off = ReadWord(interrupt * 4);
    auto seg = ReadWord(interrupt * 4 + 2);
    Operations::PushVal(this, 1, state.flags);
    Operations::PushVal(this, 1, state.sregs[CS]);
    Operations::PushVal(this, 1, state.ip);
    state.flags ^= state.flags & (IF | TF);
    state.ip = off;
    state.sregs[CS] = seg;
}

auto CPU::InitState() -> CPUState
{
    return {.sregs = {0, 0xFFFF}};
}

void CPU::SetNMI(int level)
{
    halt.store(0, std::memory_order_relaxed);
    nmi.store(level, std::memory_order_release);
}

void CPU::SetHalt(int level)
{
    halt.store(level, std::memory_order_release);
}

void CPU::SetINTR(int interrupt)
{
    halt.store(0, std::memory_order_relaxed);
    intr.store(interrupt, std::memory_order_release);
}

int CPU::DoStep()
{
    int result;
    auto prevIP = state.ip;
    try {
        oldflags &= state.flags;
        if (halt.load(std::memory_order_acquire)) {
            return Halt;
        }
        if (oldflags & TF) {
            InitInterrupt(CPUException::DB);
        }
        if (nmi.load(std::memory_order_acquire)) {
            InitInterrupt(CPUException::NMI);
        }
        if (oldflags & IF) {
            auto interrupt = intr.load(std::memory_order_acquire);
            if (interrupt != NoInterrupt) {
                InitInterrupt(interrupt);
            }
        }
        oldflags = state.flags;
        prevIP = state.ip;
        Prefixes prefixes = { 0, SegReserve };
        do {
            auto op = ReadByte(CS, state.ip++);
            result = Operations::map1[op](this, prefixes, op);
        } while (result == Continue);
        if (result == Repeat) {
            state.ip = prevIP;
        }
    } catch (CPUException& e) {
        state.ip = prevIP;
        InitInterrupt(e.GetException());
        result = Normal;
    }
    return result;
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
    return ReadByte(CalcAddr(sreg, addr));
}

uint16_t CPU::ReadWord(SegmentRegister sreg, uint16_t addr)
{
    return ReadWord(CalcAddr(sreg, addr));
}

auto CPU::ReadMem(SegmentRegister sreg, uint16_t addr, int logSz) -> RegVal
{
    RegVal result;
    switch(logSz) {
    case 0:
        result = ReadByte(CalcAddr(sreg, addr));
        break;
    case 1:
        result = ReadWord(CalcAddr(sreg, addr));
        break;
    }
    return result;
}

void CPU::WriteByte(SegmentRegister sreg, uint16_t addr, uint8_t val)
{
    WriteByte(CalcAddr(sreg, addr), val);
}

void CPU::WriteWord(SegmentRegister sreg, uint16_t addr, uint16_t val)
{
    WriteWord(CalcAddr(sreg, addr), val);
}

void CPU::WriteMem(SegmentRegister sreg, uint16_t addr, int logSz, RegVal val)
{
    switch(logSz) {
    case 0:
        WriteByte(CalcAddr(sreg, addr), val);
        break;
    case 1:
        WriteWord(CalcAddr(sreg, addr), val);
        break;
    }
}

auto CPU::ReadByte(uint32_t addr) -> uint8_t
{
    unsigned char byte;
    hook->ReadMem(state, &byte, sizeof(byte), addr);
    return byte;
}

uint16_t CPU::ReadWord(uint32_t addr)
{
    unsigned char word[2];
    hook->ReadMem(state, word, sizeof(word), addr);
    return word[1] * 0x100 + word[0];
}

auto CPU::ReadMem(uint32_t addr, int logSz) -> RegVal
{
    RegVal result = 0;
    switch(logSz) {
    case 0:
        result = ReadByte(addr);
        break;
    case 1:
        result = ReadWord(addr);
        break;
    }
    return result;
}

void CPU::WriteByte(uint32_t addr, uint8_t val)
{
    unsigned char byte = val;
    hook->WriteMem(state, addr, &byte, sizeof(byte));
}

void CPU::WriteWord(uint32_t addr, uint16_t val)
{
    unsigned char word[2];
    for (int i = 0; i < sizeof(val); ++i) {
        word[i] = val;
        val >>= 8;
    }
    hook->WriteMem(state, addr, &word, sizeof(word));
}

void CPU::WriteMem(uint32_t addr, int logSz, RegVal val)
{
    switch(logSz) {
    case 0:
        WriteByte(addr, val);
        break;
    case 1:
        WriteWord(addr, val);
        break;
    }
}

auto CPU::CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t
{
    return addr + state.sregs[sreg] * 0x10;
}

} // namespace x86emu
