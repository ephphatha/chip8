// ConsoleUI.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <bitset>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include "../Emulator/Chip8ReferenceVm.h"

#define PDC_WIDE
#define PDC_DLL_BUILD
#include <curses.h>

constexpr uint_fast8_t PIXEL_WIDTH = 2;
constexpr wchar_t ON_PIXEL[PIXEL_WIDTH] = { L'\u2588', L'\u2588' };
constexpr wchar_t OFF_PIXEL[PIXEL_WIDTH] = { ' ', ' ' };

void render_display_unit(WINDOW *window, uint_fast8_t row, uint_fast8_t col, std::byte display_unit) {
	for (auto i = 0; i < 8; ++i) {
		//using two character strings (wide characters) to make square pixels. Assuming users will have a console with monospaced fonts at approx 2*1 ratio
		mvwins_nwstr(window, row, ((col * 8) + i) * PIXEL_WIDTH, std::to_integer<bool>(display_unit & std::byte(0b10000000 >> i)) ? ON_PIXEL : OFF_PIXEL, PIXEL_WIDTH);
	}
}

void display_frame(const Chip8ReferenceVm &emulator, WINDOW *window) {
	auto row = 0, col = 0;
	for (auto display_unit : emulator.getDisplayBuffer()) {
		render_display_unit(window, row, col, display_unit);
		if (++col >= emulator.DISPLAY_WIDTH_UNITS) {
			++row;
			col = 0;
		}
	}
	wrefresh(window);
}

typedef std::unordered_map<wint_t, uint_fast8_t> keymap_type;
const keymap_type QWERTY_KEYMAP = {
	{'1',   1}, {'2',   2}, {'3',   3}, {'4', 0xC},
	{'q',   4}, {'w',   5}, {'e',   6}, {'r', 0xD},
	{'a',   7}, {'s',   8}, {'d',   9}, {'f', 0xE},
	{'z', 0xA}, {'x',   0}, {'c', 0xB}, {'v', 0xF},
};
const keymap_type HEX_KEYMAP = {
	{'1',   1}, {'2',   2}, {'3',   3}, {'c', 0xC},
	{'4',   4}, {'5',   5}, {'6',   6}, {'d', 0xD},
	{'7',   7}, {'8',   8}, {'9',   9}, {'e', 0xE},
	{'a', 0xA}, {'0',   0}, {'b', 0xB}, {'f', 0xF},
};
keymap_type keymap = QWERTY_KEYMAP;

void run(Chip8ReferenceVm &emulator, WINDOW *window) {
	keypad(window, true);
	nodelay(window, true);
	noecho();
	curs_set(0);

	unsigned int frameCount = 0;
	unsigned int instructionCount = 0;
	while (emulator.isLive()) {
		instructionCount += emulator.doFrame();
		display_frame(emulator, window);
		++frameCount;

		if (emulator.getSoundTimer()) {
			beep();
		}

		wint_t key = 0;
		int key_return = 0;

		emulator.clearKeyState();
		while ((key_return = wget_wch(window, &key)) != ERR) {
			if (key_return == OK) {
				if (keymap.contains(key)) {
					emulator.setKeyState(keymap[key], true);
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 60));
	}
}

void read_file_into_rom(std::filesystem::path &file_name, std::vector<std::byte> &rom) {
	std::ifstream file(file_name, std::ios::binary);

	std::transform(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), std::back_inserter(rom), [](char c) -> std::byte { return std::byte(c); });
}

int main(int argc, char **argv) {
	WINDOW *window = initscr();
	resize_term(Chip8ReferenceVm::DISPLAY_HEIGHT, Chip8ReferenceVm::DISPLAY_WIDTH * PIXEL_WIDTH);

	std::vector<std::byte> rom;
	if (argc > 1) {
		std::filesystem::path file_name(argv[1]);
		read_file_into_rom(file_name, rom);
	} else {
		for (int8_t byte : {
			// Screenwipe.ch8
				0xA2, 0x6E, 0x22, 0x3A, 0xA2, 0x76, 0x6D, 0x03,
				0xFD, 0x15, 0xFF, 0x07, 0x3F, 0x00, 0x12, 0x0A,
				0x22, 0x3A, 0x70, 0x01, 0x30, 0x7E, 0x12, 0x08,
				0xA2, 0x6E, 0x22, 0x3A, 0x60, 0x00, 0xA2, 0x7E,
				0x22, 0x4C, 0xA2, 0x7F, 0xFD, 0x15, 0xFF, 0x07,
				0x3F, 0x00, 0x12, 0x26, 0x22, 0x4C, 0x70, 0x01,
				0x30, 0x3F, 0x12, 0x24, 0xA2, 0x7E, 0x22, 0x4C,
				0xFF, 0x0A, 0x61, 0x00, 0xD0, 0x18, 0x61, 0x08,
				0xD0, 0x18, 0x61, 0x10, 0xD0, 0x18, 0x61, 0x18,
				0xD0, 0x18, 0x00, 0xEE, 0x61, 0x00, 0xD1, 0x03,
				0x61, 0x08, 0xD1, 0x03, 0x61, 0x10, 0xD1, 0x03,
				0x61, 0x18, 0xD1, 0x03, 0x61, 0x20, 0xD1, 0x03,
				0x61, 0x28, 0xD1, 0x03, 0x61, 0x30, 0xD1, 0x03,
				0x61, 0x38, 0xD1, 0x03, 0x00, 0xEE, 0xC0, 0xC0,
				0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xA0, 0xA0,
				0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xFF, 0xFF,
				0x00, 0xFF
		}) {
			rom.push_back(static_cast<std::byte>(byte));
		}
	}

	Chip8ReferenceVm emulator(rom);
	emulator.setEmulationSpeed(500);
	run(emulator, window);

	endwin();
	return 0;
}
