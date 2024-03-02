#ifndef CPU86E_IIOHOOK_H
#define CPU86E_IIOHOOK_H

#include <cstdint>
#include <cstdlib>

namespace cpu86e {

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::size_t;

struct CPUState;

struct IIOHook
{
    virtual void ReadMem(CPUState& state, void* data, size_t size, uint32_t addr) = 0;
    virtual void WriteMem(CPUState& state, uint32_t addr, void* data, size_t size) = 0;
    virtual auto ReadIOByte(uint32_t addr) -> uint8_t = 0;
    virtual auto ReadIOWord(uint32_t addr) -> uint16_t = 0;
    virtual void WriteIOByte(uint32_t addr, uint8_t val) = 0;
    virtual void WriteIOWord(uint32_t addr, uint16_t val) = 0;
    static constexpr int NoInterrupt = -1;
    virtual int InterruptCheck() = 0;
};

} // namespace x86emu

#endif // CPU86E_IIOHOOK_H
