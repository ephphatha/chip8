#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <random>
#include <stack>
#include <span>
#include <thread>

class Chip8ReferenceVm
{
public:
	Chip8ReferenceVm(const std::vector<std::byte>& rom);
	~Chip8ReferenceVm();

	// Set an upper limit on how many instructions per tick should be emulated (0 [default] disables the limit)
	void setEmulationSpeed(unsigned long);

	constexpr bool isRunning() const {
		return this->state == State::Running;
	}

	constexpr bool isLive() const {
		return this->state != State::Halted;
	}

	void step();

	unsigned long doFrame();

	void setKeyState(uint_fast8_t keyCode, bool isPressed);
	void clearKeyState();

	static constexpr uint_fast8_t DISPLAY_WIDTH = 64;
	static constexpr uint_fast8_t DISPLAY_WIDTH_UNITS = DISPLAY_WIDTH / 8;
	static constexpr uint_fast8_t DISPLAY_HEIGHT = 32;
	using Display = std::array<std::byte, DISPLAY_WIDTH_UNITS * DISPLAY_HEIGHT>;
	const Display& getDisplayBuffer() const;

	using Timer = uint_fast8_t;
	const Timer getSoundTimer() const;

protected:
	// Program Memory
	//  0x000-0x1FF and 0xE90-0xFFF are reserved on various implementations but at least on Octo all bytes are writable. No write/execute protection is implemented.
	using RAM = std::array<std::byte, 4096>;
	RAM ram;

	// Program Counter
	//  As memory will only ever be read via the program counter it is implemented as a const_iterator.
	using ProgramCounter = RAM::const_iterator;
	ProgramCounter pc;

	struct Instruction {
		std::byte hi;
		std::byte lo;
	};
	Instruction getInstruction();

	/**
	* Skip the next instruction.
	*/
	void skip();

	// A 4 bit value
	using ShortValue = uint_fast8_t;
	static constexpr inline ShortValue getShortValueLo(const std::byte&);
	static constexpr inline ShortValue getShortValueHi(const std::byte&);

	// An 8 bit value
	using Value = uint_fast8_t;
	static constexpr inline Value getValue(const std::byte&);

	// A 12 bit value
	using LongValue = uint_fast16_t;
	static constexpr inline LongValue getLongValue(const std::byte& byte_hi, const std::byte& byte_lo);

	/**
	* Jump to a specific address.
	* 
	* @param target Address as a 12 bit integer value, treated as an offset from the start of address space.
	*/
	constexpr void jump(LongValue target);

	std::stack<ProgramCounter> call_stack;

	/**
	* Jump to an address while saving the current location on the call stack for a future return.
	* 
	* @param target Address as a 12 bit integer value, treated as an offset from the start of address space.
	*/
	void call(LongValue target);

	/**
	* Jumps back to the last call site.
	* 
	* No sanity checking is performed, if a program attempts to return more times than it actually performs subroutine calls then this will crash.
	*/
	void doReturn();

	// Address Register
	//  Used for both reads and writes via save/load instructions
	using Address = RAM::iterator;
	Address i;

	/**
	* Set the address register to a specific location.
	* 
	* @param An iterator representing a location in RAM.
	*/
	void setAddressRegister(Address);

	/**
	* Set the address register to a specific location based on an integer value
	* 
	* @param target Address as a 12 bit integer value, treated as an offset from the start of address space.
	*/
	void setAddressRegister(LongValue target);

	/**
	* Increment the current address by an offset
	*/
	void incrementAddressRegister(Value offset);

	// Data Registers
	// Referenced as v0-vF from begin to end. vF will be trampled by many instructions.
	using Register = std::byte;
	using RegisterBank = std::array<Register, 16>;
	RegisterBank v{ std::byte{0} };

	// "Pointer" to the start of font data.
	//  Declared as an iterator type instead of a const_iterator because Chip8 has no concept of protected/read only memory.
	//  The FX29 (load address of font character) instruction will use this as the base address for convenience, so we can just increment the iterator and set i directly.
	//  Also while I'm not sure if any programs actually do so, potentially a rom might want to alter the font characters when it loads or trample over this region of
	//  memory because it was written for a specific platform where it was unused by the emulator.
	Address font_offset = this->ram.begin() + 0x50; // By convention fonts are stored starting from 0x50

	Address rom_offset = this->ram.begin() + 0x200; // Roms are loaded starting at address 0x200, all jumps will be based on this so don't deviate.

	// Timers.
	// Both timers count down at 60hz.
	Timer delay = 0;
	Timer sound = 0; // Sound will play iff this value is greater than 1
	std::thread timer_thread;

	unsigned long frame_limit = 0;

	const std::chrono::milliseconds tick_interval = std::chrono::milliseconds(1000 / 60);
	void runTimers();

	const std::byte getRandomByte();

	// Internal helpers
	// Key map, each bit corresponds to a key on the hex input device where 1 is pressed and 0 is released.
	std::bitset<16> keys = 0;

	bool isKeyPressed(const uint_fast8_t& x) const;

	// Display Buffer
	//  64*32 pixels, with each pixel being a single bit. This could possibly be a bitset but that seems like a headache to copy into.
	Display display;

	void drawSprite(uint_fast8_t, uint_fast8_t, uint_fast8_t);

	// Random number generation internals
	std::default_random_engine random;
	std::uniform_int_distribution<unsigned short> distribution;

	enum class State {
		Loading,
		Running,
		Blocked,
		Halted
	};
	State state = State::Loading;

	uint_fast8_t keypress_target_register = -1;

private:
	std::random_device rd;
};

