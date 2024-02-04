#ifndef X86EMU_IIOHOOK_H
#define X86EMU_IIOHOOK_H

#include <cstdint>

namespace x86emu {

using std::uint8_t;
using std::uint16_t;
using std::uint32_t;

struct IIOHook
{
    virtual auto ReadMemByte(uint32_t addr) -> uint8_t = 0;
    virtual auto ReadMemWord(uint32_t addr) -> uint16_t = 0;
    virtual void WriteMemByte(uint32_t addr, uint8_t val) = 0;
    virtual void WriteMemWord(uint32_t addr, uint16_t val) = 0;
    virtual auto ReadIOByte(uint32_t addr) -> uint8_t = 0;
    virtual auto ReadIOWord(uint32_t addr) -> uint16_t = 0;
    virtual void WriteIOByte(uint32_t addr, uint8_t val) = 0;
    virtual void WriteIOWord(uint32_t addr, uint16_t val) = 0;
};

} // namespace x86emu

#endif // X86EMU_IIOHOOK_H
