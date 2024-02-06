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

}

CPU::CPU()
{}

CPU::CPU(const CPUState &initState) :
    state(initState)
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
    }

    static void AddB(CPU* cpu, Prefixes prefixes)
    {
        ModRM operands = GetModRM(cpu);
        auto regs = cpu->state.gpr;
        if (operands.type != Reg) {
            auto sreg = GetSeg(prefixes, operands.type);
            auto t = cpu->ReadByte(sreg, operands.addr);
            uint8_t r = t + regs[operands.reg];
            auto zero = r == 0;
            auto carry = r < t;
            auto sign = !!(r & 0x80);
            auto ovfl = t & uint8_t(regs[operands.reg]) & 0x80 && !sign;
            cpu->WriteByte(sreg, operands.addr, r);
        }
    }

    using Op = void(CPU*, Prefixes);
    Op* map1[256] = {
        AddB
    };
};

int CPU::DoOpcode()
{
    auto prevIP = state.ip;
    Prefixes prefixes = ParsePrefixes();
    auto op = ReadByte(CS, state.ip++);
    if (op < 0x40) {
        if ((op & 6) == 6) {
            auto r = SegmentRegister(op & 0x18);
            using P = void(CPU::*)(SegmentRegister);
            constexpr P f[] = { &CPU::PushSreg, &CPU::PopSreg };
            (this->*(f[op & 1]))(r);
        }
    }
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

auto CPU::CalcAddr(SegmentRegister sreg, uint16_t addr) -> uint32_t
{
    return addr + state.sregs[sreg] * 0x10;
}

} // namespace x86emu
