#include <iostream>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

int main() {
    const char* device = "/dev/ttyS1";  // UART1
    int uart = open(device, O_RDWR | O_NOCTTY | O_NDELAY);

    if (uart == -1) {
        perror("Unable to open UART");
        return 1;
    }

    struct termios options;
    tcgetattr(uart, &options);

    // Configure: 115200 baud, 8N1, no flow control
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag &= ~PARENB; // No parity
    options.c_cflag &= ~CSTOPB; // 1 stop bit
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;     // 8 bits
    options.c_cflag &= ~CRTSCTS;// No flow control
    options.c_cflag |= CREAD | CLOCAL;
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    tcsetattr(uart, TCSANOW, &options);

    // Get input from user
    std::string userInput;
    std::cout << "Enter a message to send via UART: ";
    std::getline(std::cin, userInput);

    // Send to UART
    int count = write(uart, userInput.c_str(), userInput.length());

    if (count < 0) {
        perror("UART TX error");
        return 1;
    }

    close(uart);
    return 0;
}
