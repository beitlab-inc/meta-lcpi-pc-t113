#define INPUT_LINE  128  // change to your input GPIO number
#define OUTPUT_LINE 129  // change to your output GPIO number


#include <gpiod.h>
#include <iostream>
#include <unistd.h>
#include <vector>

#define CHIPNAME "gpiochip0"
#define CONSUMER "matrix-scanner"

// 74HC595 GPIO control (adjust as needed)
#define PIN_DATA  128 // PE0
#define PIN_CLOCK 129 // PE1
#define PIN_LATCH 132 // PE4

// Input GPIOs for rows PE5 - PE10
const int INPUT_ROWS[6] = {133, 134, 135, 136, 137, 138};

gpiod_chip* chip;
gpiod_line *data, *clk_shift, *latch;
std::vector<gpiod_line*> inputs;

void pulse(gpiod_line* pin) {
    gpiod_line_set_value(pin, 1);
    usleep(1);
    gpiod_line_set_value(pin, 0);
    usleep(1);
}

// Sends a byte to the 74HC595
void shiftOut(uint8_t value) {
    for (int i = 7; i >= 0; --i) {
        gpiod_line_set_value(data, ~(value >> i) & 1);
        pulse(clk_shift);
    }
    pulse(latch);
}

// Initialize GPIOs
bool setup() {
    chip = gpiod_chip_open_by_name(CHIPNAME);
    if (!chip) return false;

    data  = gpiod_chip_get_line(chip, PIN_DATA);
    clk_shift = gpiod_chip_get_line(chip, PIN_CLOCK);
    latch = gpiod_chip_get_line(chip, PIN_LATCH);

    if (!data || !clk_shift || !latch) return false;

    if (gpiod_line_request_output(data,  CONSUMER, 0) < 0 ||
        gpiod_line_request_output(clk_shift, CONSUMER, 0) < 0 ||
        gpiod_line_request_output(latch, CONSUMER, 0) < 0)
        return false;

    for (int i = 0; i < 6; ++i) {
        gpiod_line* line = gpiod_chip_get_line(chip, INPUT_ROWS[i]);
        if (!line || gpiod_line_request_input(line, CONSUMER) < 0) return false;
        inputs.push_back(line);
    }

    return true;
}

// Clean up
void cleanup() {
    gpiod_chip_close(chip);
}

int main() {
    std::cout << "Hola soy piano feliz\n";
    if (!setup()) {
        std::cerr << "Failed to setup GPIOs\n";
        return 1;
    }

    std::vector<std::vector<bool>> prev_state(6, std::vector<bool>(8, false));

    while (true) {
        bool any_pressed = false;

        for (int col = 0; col < 8; ++col) {
            shiftOut(1 << col);  // Activate one column

            usleep(100);  // Short delay to settle

            for (int row = 0; row < 6; ++row) {
                int val = gpiod_line_get_value(inputs[row]);

                bool pressed = (val == 0);  // active-low

                if (pressed && !prev_state[row][col]) {
                    std::cout << "Key pressed: [" << row << "][" << col << "]" << std::endl;
                }

                prev_state[row][col] = pressed;
                any_pressed |= pressed;
            }
        }

        if (!any_pressed) {
            usleep(100000); // Sleep more if idle
        } else {
            usleep(10000);
        }
    }

    cleanup();
    return 0;
}

