#include "Chip8ReferenceVm.h"

#include <chrono>

template<typename... Ts>
std::array<std::byte, sizeof...(Ts)> make_bytes(Ts&&... args) noexcept {
	return{ std::byte(std::forward<Ts>(args))... };
}

Chip8ReferenceVm::Chip8ReferenceVm(const std::vector<std::byte>& rom) :
	random(rd()),
	pc(ram.cbegin() + 0x200),
	i(ram.begin()),
	timer_thread(&Chip8ReferenceVm::runTimers, std::ref(this->sound), std::ref(this->delay))
{

	auto font = make_bytes(
		// 0
		0b11110000,
		0b10010000,
		0b10010000,
		0b10010000,
		0b11110000,

		// 1
		0b00100000,
		0b01100000,
		0b00100000,
		0b00100000,
		0b01010000,

		// 2
		0b11110000,
		0b00010000,
		0b11110000,
		0b10000000,
		0b11110000,

		// 3
		0b11110000,
		0b00010000,
		0b11110000,
		0b00010000,
		0b11110000,

		// 4
		0b10010000,
		0b10010000,
		0b11110000,
		0b00010000,
		0b00010000,

		// 5
		0b11110000,
		0b10000000,
		0b11110000,
		0b00010000,
		0b11110000,

		// 6
		0b11110000,
		0b10000000,
		0b11110000,
		0b10010000,
		0b11110000,

		// 7
		0b11110000,
		0b00010000,
		0b00100000,
		0b01000000,
		0b01000000,

		// 8
		0b11110000,
		0b10010000,
		0b11110000,
		0b10010000,
		0b11110000,

		// 9
		0b11110000,
		0b10010000,
		0b11110000,
		0b00010000,
		0b11110000,

		// A
		0b11110000,
		0b10010000,
		0b11110000,
		0b10010000,
		0b10010000,

		// B
		0b11100000,
		0b10010000,
		0b11100000,
		0b10010000,
		0b11100000,

		// C
		0b11110000,
		0b10000000,
		0b10000000,
		0b10000000,
		0b11110000,

		// D
		0b11100000,
		0b10010000,
		0b10010000,
		0b10010000,
		0b11100000,

		// E
		0b11110000,
		0b10000000,
		0b11110000,
		0b10000000,
		0b11110000,

		// F
		0b11110000,
		0b10000000,
		0b11110000,
		0b10000000,
		0b10000000
	);

	std::move(font.begin(), font.end(), this->font_offset);

	std::copy(rom.begin(), rom.end(), this->rom_offset);

	this->state = State::Running;
}

Chip8ReferenceVm::~Chip8ReferenceVm() {
	this->state = State::Halted;
}

constexpr inline auto lo_nybble(const std::byte& byte) {
	return byte & std::byte{ 0x0F };
}

constexpr inline auto hi_nybble(const std::byte& byte) {
	return byte >> 4;
}

void Chip8ReferenceVm::step() {
	if (!this->isRunning()) {
		return;
	}

	auto instruction = this->getInstruction();

	switch (hi_nybble(instruction.hi))
	{
	case std::byte{ 0x0 }:
		//0NNN Execute machine language subroutine at address NNN
		switch (getLongValue(instruction.hi, instruction.lo))
		{
		case 0x000:
			//0000 Is implemented in Octo as halt
			//this->running = false;
			break;

		case 0x0E0:
			//00E0 Clear the screen
			this->display.fill(std::byte{ 0 });
			break;

		case 0x0EE:
			//00EE Return from a subroutine
			this->doReturn();
			break;

		default:
			// Unimplemented
			break;
		}
		break;

	case std::byte{ 0x1 }: {
		//1NNN Jump to address NNN
		this->jump(getLongValue(instruction.hi, instruction.lo));
		break;
	}

	case std::byte{ 0x2 }: {
		//2NNN Execute subroutine starting at address NNN
		this->call(getLongValue(instruction.hi, instruction.lo));
		break;
	}

	case std::byte{ 0x3 }: {
		auto x = getShortValueLo(instruction.hi);
		//3XNN Skip the following instruction if the value of register VX equals NN
		if (this->v.at(x) == instruction.lo) {
			this->skip();
		}
		break;
	}

	case std::byte{ 0x4 }: {
		auto x = getShortValueLo(instruction.hi);
		//4XNN Skip the following instruction if the value of register VX is not equal to NN
		if (this->v.at(x) != instruction.lo) {
			this->skip();
		}
		break;
	}

	case std::byte{ 0x5 }: {
		auto x = getShortValueLo(instruction.hi);
		auto y = getShortValueHi(instruction.lo);
		//5XY0 Skip the following instruction if the value of register VX is equal to the value of register VY
		if (this->v.at(x) == this->v.at(y)) {
			this->skip();
		}
		break;
	}

	case std::byte{ 0x6 }: {
		auto x = getShortValueLo(instruction.hi);
		//6XNN Store number NN in register VX
		this->v.at(x) = instruction.lo;
		break;
	}

	case std::byte{ 0x7 }: {
		auto x = getShortValueLo(instruction.hi);
		//7XNN Add the value NN to register VX
		// NOTE: Overflows do not set VF
		this->v.at(x) = static_cast<std::byte>(getValue(this->v.at(x)) + getValue(instruction.lo));
		break;
	}

	case std::byte{ 0x8 }: {
		auto x = getShortValueLo(instruction.hi);
		auto y = getShortValueHi(instruction.lo);
		switch (lo_nybble(instruction.lo))
		{
		case std::byte{ 0x0 }:
			//8XY0 Store the value of register VY in register VX
			this->v.at(x) = this->v.at(y);
			break;

		case std::byte{ 0x1 }:
			//8XY1 Set VX to VX OR VY
			this->v.at(x) |= this->v.at(y);
			break;

		case std::byte{ 0x2 }:
			//8XY2 Set VX to VX AND VY
			this->v.at(x) &= this->v.at(y);
			break;

		case std::byte{ 0x3 }:
			//8XY3 Set VX to VX XOR VY
			this->v.at(x) ^= this->v.at(y);
			break;

		case std::byte{ 0x4 }: {
			//8XY4 Add the value of register VY to register VX
			//     Set VF to 01 if a carry occurs
			//     Set VF to 00 if a carry does not occur
			auto wide_val = std::to_integer<LongValue>(this->v.at(x)) + std::to_integer<LongValue>(this->v.at(y));
			this->v.at(x) = static_cast<std::byte>(wide_val);
			this->v.at(0xF) = static_cast<std::byte>(wide_val >> 8);
			break;
		}

		case std::byte{ 0x5 }: {
			//8XY5 Subtract the value of register VY from register VX
			//     Set VF to 00 if a borrow occurs
			//     Set VF to 01 if a borrow does not occur
			auto wide_val = (0x100 & std::to_integer<LongValue>(this->v.at(x))) - std::to_integer<LongValue>(this->v.at(y));
			this->v.at(x) = static_cast<std::byte>(wide_val);
			this->v.at(0xF) = static_cast<std::byte>(wide_val >> 8);
			break;
		}

		case std::byte{ 0x6 }: {
			//8XY6 Store the value of register VY shifted right one bit in register VX¹
			//     Set register VF to the least significant bit prior to the shift
			//     VY is unchanged
			auto val = this->v.at(y);
			this->v.at(x) = val >> 1;
			this->v.at(0xF) = val & std::byte{ 0x1 };
			break;
		}

		case std::byte{ 0x7 }: {
			//8XY7 Set register VX to the value of VY minus VX
			//     Set VF to 00 if a borrow occurs
			//     Set VF to 01 if a borrow does not occur
			auto wide_val = (0x100 & std::to_integer<LongValue>(this->v.at(y))) - std::to_integer<LongValue>(this->v.at(x));
			this->v.at(x) = static_cast<std::byte>(wide_val);
			this->v.at(0xF) = static_cast<std::byte>(wide_val >> 8);
			break;
		}

		case std::byte{ 0xE }: {
			//8XYE Store the value of register VY shifted left one bit in register VX¹
			//     Set register VF to the most significant bit prior to the shift
			//     VY is unchanged
			auto wide_val = std::to_integer<LongValue>(this->v.at(y)) << 1;
			this->v.at(x) = static_cast<std::byte>(wide_val);
			this->v.at(0xF) = static_cast<std::byte>(wide_val >> 8);
			break;
		}

		default:
			// Unsupported instruction
			break;
		}
		break;
	}

	case std::byte{ 0x9 }: {
		auto x = getShortValueLo(instruction.hi);
		auto y = getShortValueHi(instruction.lo);
		//9XY0 Skip the following instruction if the value of register VX is not equal to the value of register VY
		if (this->v.at(x) != this->v.at(y)) {
			this->skip();
		}
		break;
	}

	case std::byte{ 0xA }: {
		//ANNN Store memory address NNN in register I
		this->setAddressRegister(getLongValue(instruction.hi, instruction.lo));
		break;
	}

	case std::byte{ 0xB }:
		//BNNN Jump to address NNN + V0
		this->jump(getLongValue(instruction.hi, instruction.lo) + std::to_integer<LongValue>(this->v.at(0x0)));
		break;

	case std::byte{ 0xC }: {
		auto x = getShortValueLo(instruction.hi);
		//CXNN Set VX to a random number with a mask of NN
		this->v.at(x) = this->getRandomByte() & instruction.lo;
		break;
	}

	case std::byte{ 0xD }: {
		auto x = getShortValueLo(instruction.hi);
		auto y = getShortValueHi(instruction.lo);
		//DXYN Draw a sprite at position VX, VY with N bytes of sprite data starting at the address stored in I
		//     Set VF to 01 if any set pixels are changed to unset, and 00 otherwise
		this->drawSprite(getValue(this->v.at(x)), getValue(this->v.at(y)), getShortValueLo(instruction.lo));
		break;
	}

	case std::byte{ 0xE }: {
		// Key operations based on the keycode stored in register VX
		auto x = getValue(this->v.at(getShortValueLo(instruction.hi)));

		switch (instruction.lo)
		{
		case std::byte{ 0x9E }:
			//EX9E	Skip the following instruction if the key corresponding to the hex value currently stored in register VX is pressed
			if (isKeyPressed(x)) {
				this->skip();
			}
			break;

		case std::byte{ 0xA1 }:
			//EXA1	Skip the following instruction if the key corresponding to the hex value currently stored in register VX is not pressed
			if (!isKeyPressed(x)) {
				this->skip();
			}
			break;

		default:
			// Unsupported instruction
			break;
		}
		break;
	}

	case std::byte{ 0xF }: {
		auto x = getShortValueLo(instruction.hi);

		switch (instruction.lo)
		{
		case std::byte{ 0x07 }:
			//FX07 Store the current value of the delay timer in register VX
			this->v.at(x) = static_cast<std::byte>(this->delay);
			break;

		case std::byte{ 0x0A }:
			//FX0A Wait for a keypress and store the result in register VX
			this->keypress_target_register = x;
			this->state = State::Blocked;
			// The emulator will return immediately for any further calls to step() until a key is received.
			break;

		case std::byte{ 0x15 }:
			//FX15 Set the delay timer to the value of register VX
			this->delay = static_cast<Timer>(this->v.at(x));
			break;

		case std::byte{ 0x18 }:
			//FX18 Set the sound timer to the value of register VX
			this->sound = static_cast<Timer>(this->v.at(x));
			break;

		case std::byte{ 0x1E }:
			//FX1E Add the value stored in register VX to register I
			this->incrementAddressRegister(getValue(this->v.at(x)));
			break;

		case std::byte{ 0x29 }:
			//FX29 Set I to the memory address of the sprite data corresponding to the hexadecimal digit stored in register VX
			this->setAddressRegister(this->font_offset + 5 * std::to_integer<ptrdiff_t>(this->v.at(x) & std::byte(0xF)));
			break;

		case std::byte{ 0x33 }: {
			//FX33 Store the binary - coded decimal equivalent of the value stored in register VX at addresses I, I + 1, and I + 2
			auto val = getValue(this->v.at(x));
			*this->i = std::byte(val / 100 % 10);
			*(this->i + 1) = std::byte(val / 10 % 10);
			*(this->i + 2) = std::byte(val % 10);
			break;
		}

		case std::byte{ 0x55 }:
			//FX55 Store the values of registers V0 to VX inclusive in memory starting at address I
			//     I is set to I + X + 1 after operation²
			std::copy(this->v.cbegin(), this->v.cbegin() + x + 1, this->i);
			this->incrementAddressRegister(x + 1);
			break;

		case std::byte{ 0x65 }: {
			//FX65 Fill registers V0 to VX inclusive with the values stored in memory starting at address I
			//     I is set to I + X + 1 after operation²
			const auto start_i = this->i;
			this->incrementAddressRegister(x + 1);
			std::copy(start_i, this->i, this->v.begin());
			break;
		}

		default:
			// Unsupported instruction
			break;
		}
		break;
	}

	default:
		// Unreachable
		break;
	}
}

bool Chip8ReferenceVm::isKeyPressed(const uint_fast8_t& x) const {
	return this->keys.test(x);
}

constexpr uint_fast8_t NO_KEY = -1;
void Chip8ReferenceVm::setKeyState(uint_fast8_t key, bool pressed) {
	if (pressed) {
		if (this->keypress_target_register != NO_KEY) {
			// set key register to value. It is not defined what should happen if multiple keys are being held when a wait for keypress instruction is executed, this will use whatever the input device handler happens to give us first.
			this->v.at(this->keypress_target_register) = std::byte(key);
			this->keypress_target_register = NO_KEY;
			this->state = State::Running;
		}
		this->keys.set(key);
	}
	else {
		this->keys.reset(key);
	}
}

void Chip8ReferenceVm::clearKeyState() {
	this->keys.reset();
}

unsigned long Chip8ReferenceVm::doFrame() {
	unsigned long instructions_executed = 0;

	auto start_time = std::chrono::steady_clock::now();

	while (this->isRunning() &&
		(this->frame_limit == 0 || instructions_executed < this->frame_limit)) {
		this->step();
		++instructions_executed;

		auto time_elapsed = std::chrono::steady_clock::now() - start_time;

		if (time_elapsed >= this->tick_interval && instructions_executed > 0) {
			break;
		}
	}

	return instructions_executed;
}

void Chip8ReferenceVm::setEmulationSpeed(unsigned long target_speed) {
	this->frame_limit = target_speed;
}

const Chip8ReferenceVm::Display& Chip8ReferenceVm::getDisplayBuffer() const {
	return this->display;
}

const Chip8ReferenceVm::Timer Chip8ReferenceVm::getSoundTimer() const {
	return this->sound;
}

Chip8ReferenceVm::Instruction Chip8ReferenceVm::getInstruction() {
	Instruction instruction;
	if (this->pc != this->ram.cend()) {
		instruction.hi = *this->pc++;
	}
	if (this->pc != this->ram.cend()) {
		instruction.lo = *this->pc++;
		return instruction;
	}
	this->state = State::Halted;
	return {};
}

void Chip8ReferenceVm::skip() {
	this->getInstruction();
}

constexpr inline Chip8ReferenceVm::ShortValue Chip8ReferenceVm::getShortValueLo(const std::byte &byte) {
	return std::to_integer<ShortValue>(lo_nybble(byte));
}

constexpr inline Chip8ReferenceVm::ShortValue Chip8ReferenceVm::getShortValueHi(const std::byte &byte) {
	return std::to_integer<ShortValue>(hi_nybble(byte));
}

constexpr inline Chip8ReferenceVm::Value Chip8ReferenceVm::getValue(const std::byte &byte) {
	return std::to_integer<Value>(byte);
}

constexpr inline Chip8ReferenceVm::LongValue Chip8ReferenceVm::getLongValue(const std::byte &byte_hi, const std::byte &byte_lo) {
	return LongValue{ 0xFFF } & (std::to_integer<LongValue>(lo_nybble(byte_hi)) << 8 | std::to_integer<LongValue>(byte_lo));
}

constexpr void Chip8ReferenceVm::jump(LongValue target) {
	this->pc = this->ram.cbegin() + target;
}

void Chip8ReferenceVm::call(LongValue target) {
	call_stack.push(this->pc);
	this->jump(target);
}

void Chip8ReferenceVm::doReturn() {
	if (this->call_stack.empty()) {
		return;
	}

	this->pc = call_stack.top();
	call_stack.pop();
}

void Chip8ReferenceVm::drawSprite(uint_fast8_t x, uint_fast8_t y, uint_fast8_t lines) {
	this->v.at(0xF) = std::byte{ 0 }; // Clear VF

	// Wrap x/y to screen space
	auto display_col = x % Chip8ReferenceVm::DISPLAY_WIDTH;
	auto display_row = y % Chip8ReferenceVm::DISPLAY_HEIGHT;

	// offset into a display unit
	uint_fast8_t subpixels = display_col % 8;

	// Make looking up target bytes easier
	display_col = display_col / 8;

	for (auto sprite_data : std::span{ this->i, lines }) {
		std::byte sprite_line[2]{ sprite_data >> subpixels, sprite_data << (8 - subpixels) };

		uint_fast8_t display_index = display_row * Chip8ReferenceVm::DISPLAY_WIDTH_UNITS + display_col;
		this->display.at(display_index) ^= sprite_line[0]; // Draw the first part of this line of the sprite
		this->v.at(0xF) |= (this->display.at(display_index) ^ sprite_line[0]) != std::byte{ 0 } ? std::byte{ 0x1 } : std::byte{ 0 }; // If the previous line set a lit pixel to off, this XOR will leave a non-0 value in VF.

		// Wrap sprites that would be drawn past the right extent of the screen back to the left of the same row
		// Note: This is the original spec but certain extensions/implementations such as superchip do not wrap sprites.
		display_index = display_row * Chip8ReferenceVm::DISPLAY_WIDTH_UNITS + (display_col + 1) % Chip8ReferenceVm::DISPLAY_WIDTH_UNITS;
		this->display.at(display_index) ^= sprite_line[1]; // Draw any overflow
		this->v.at(0xF) |= (this->display.at(display_index) ^ sprite_line[0]) != std::byte{ 0 } ? std::byte{ 0x1 } : std::byte{ 0 }; // As above, set VF if a pixel was turned off

		// Wrap sprites that would be drawn past the bottom extent of the screen back to the top
		// Note: This is the original spec but certain extensions/implementations such as superchip do not wrap sprites.
		display_row = (display_row + 1) % Chip8ReferenceVm::DISPLAY_HEIGHT;
	}
}

void Chip8ReferenceVm::setAddressRegister(Address i) {
	this->i = i;
}

void Chip8ReferenceVm::setAddressRegister(LongValue target) {
	this->i = this->ram.begin() + target;
}

void Chip8ReferenceVm::incrementAddressRegister(Value offset) {
	this->i += offset;
}

void Chip8ReferenceVm::runTimers(std::stop_token token, Chip8ReferenceVm::Timer &sound, Chip8ReferenceVm::Timer &delay) {
	auto last_tick = std::chrono::steady_clock::now();
	while (!token.stop_requested()) {
		auto time_since_last_tick = std::chrono::steady_clock::now() - last_tick;
		do {
			std::this_thread::sleep_for(tick_interval - time_since_last_tick);
			time_since_last_tick = std::chrono::steady_clock::now() - last_tick;
		} while (time_since_last_tick < tick_interval);

		last_tick += tick_interval;

		if (sound > 0) {
			--sound;
		}

		if (delay > 0) {
			--delay;
		}
	}
}

const std::byte Chip8ReferenceVm::getRandomByte() {
	return static_cast<std::byte>(this->distribution(this->random));
}