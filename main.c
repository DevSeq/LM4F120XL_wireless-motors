// include the StellarisWare directory on the compiler path (Build / GNU Compiler / Directories)
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"

#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "driverlib/uart.h"
#include "utils/uartstdio.h"
#include <stdlib.h>

#define RED_LED   GPIO_PIN_1
#define BLUE_LED  GPIO_PIN_2
#define GREEN_LED GPIO_PIN_3

// set up PWM for the 2 motors
//https://trandi.wordpress.com/2016/03/28/ball-balancing-v2/

///////////// Console ~ UART0 ///////////////////////////////////////

// UART0 is mapped by the Stellaris Launchpad to the Virtual COM port available through Debug USB
void initConsole() {
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	UARTStdioConfig(0, 115200, SysCtlClockGet());

	UARTprintf("UART Console initialised.\n");
}

/////////////// AUREL Wireless Commands - UART5 /////////////////////////////////////

volatile char _prevChar;
volatile tBoolean _readingMsg = false;
volatile char _msg[6];
volatile char _msgPos;
volatile tBoolean _validData = false;
volatile int _joystickX;
volatile int _joystickY;

tBoolean startNewMsg(char c) {
	tBoolean res = (_prevChar == 0) && (c == 255);
	_prevChar = c;
	return res;
}

void UART5IntHandler() {
	char currChar;
	while (UARTCharsAvail(UART5_BASE)) {
		currChar = UARTCharGet(UART5_BASE);

		if (startNewMsg(currChar)) {
			_readingMsg = true;
			_msgPos = 0;
		} else if (_readingMsg) {
			if (_msgPos >= 6) {
				// data finished, last byte is the CRC
				char crc = 0;
				for (char i = 0; i < 6; i++)
					crc += _msg[i];

				if (crc == currChar) {
					_joystickX = _msg[0];
					_joystickY = _msg[1];
					_validData = true;
				} else {
					_validData = false;
					UARTprintf("Wrong CRC: %d Expected: %d\n", currChar, crc);
				}

				_readingMsg = false;
			} else {
				// normal data, add it to the message
				_msg[_msgPos++] = currChar;
			}
		}
	}
}

void initCommandsUART() {
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	GPIOPinConfigure(GPIO_PE4_U5RX);
	GPIOPinConfigure(GPIO_PE5_U5TX);	// normally not needed as only receiving
	GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_4 | GPIO_PIN_5);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART5);

	// speed of AUREL wireless is 9600bps 8-N-1
	UARTConfigSetExpClk(UART5_BASE, SysCtlClockGet(), 9600,
	UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE | UART_CONFIG_WLEN_8);

	UARTIntRegister(UART5_BASE, UART5IntHandler);
	UARTIntEnable(UART5_BASE, UART_INT_RT | UART_INT_RX);
	UARTEnable(UART5_BASE);

	UARTprintf("UART AUREL interface initialised.\n");
}

///////////////// 3 colour LEDs on the board ///////////////////////////////

void initLEDs() {
	// Enable and configure the GPIO port for the LED operation.
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, RED_LED | BLUE_LED | GREEN_LED);
	GPIOPinWrite(GPIO_PORTF_BASE, RED_LED | BLUE_LED | GREEN_LED, 0x00);
}

///////////////// PWMs for the motors ///////////////////////////////////

void initPWM() {
	// we need PB0,1,5 and PD0,1,2 - for PWM but also simpul LOW/HIGH for direction of rotation
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB | SYSCTL_PERIPH_GPIOD);
	GPIOPinTypeGPIOOutput(GPIO_PORTB_BASE,
			GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5);
	GPIOPinTypeGPIOOutput(GPIO_PORTD_BASE,
			GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2);
	// all to LOW
	GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5, 0x00);
	GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2, 0x00);

	// Timer 2 - T2CCP0 / PB0 & T2CCP1 / PB1
	// Pin Mux-ing
	GPIOPinConfigure(GPIO_PB0_T2CCP0);
	GPIOPinConfigure(GPIO_PB1_T2CCP1);
	// give control of the pins to the Timer hardware
	GPIOPinTypeTimer(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER2);
	// split the 32bit timer into 2x16 bits
	TimerConfigure(TIMER2_BASE,
			TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_PWM | TIMER_CFG_B_PWM);
	// invert the PWM
	TimerControlLevel(TIMER2_BASE, TIMER_A, true);
	TimerControlLevel(TIMER2_BASE, TIMER_B, true);

	TimerLoadSet(TIMER2_BASE, TIMER_A, 100);
	TimerEnable(TIMER2_BASE, TIMER_A);
	TimerLoadSet(TIMER2_BASE, TIMER_B, 100);
	TimerEnable(TIMER2_BASE, TIMER_B);

	UARTprintf("PWM for motors initialised.\n");
}

int constrainPercentage(int percentage) {
	if (percentage < 0)
		return 0;
	else if (percentage >= 100)
		return 99;
	else
		return percentage;
}

// Speed is [-100, 100]
// PB0 is for speed, PB5/PD0 for direction
void setMotorA(int speed) {
	if (speed < 0) {
		GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_5, 0x00);
		GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, 0xFF);
	} else {
		GPIOPinWrite(GPIO_PORTB_BASE, GPIO_PIN_5, 0xFF);
		GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_0, 0x00);
	}

	// PWM Timer T2CCP0 / PB0
	TimerMatchSet(TIMER2_BASE, TIMER_A, constrainPercentage(abs(speed)));
}

// Speed is [-100, 100]
// PB1 is for speed, PD1/PD2 for direction
void setMotorB(int speed) {
	if (speed < 0) {
		GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_1, 0x00);
		GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, 0xFF);
	} else {
		GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_1, 0xFF);
		GPIOPinWrite(GPIO_PORTD_BASE, GPIO_PIN_2, 0x00);
	}

	// PWM Timer T2CCP1 / PB1
	TimerMatchSet(TIMER2_BASE, TIMER_B, constrainPercentage(abs(speed)));
}

///////////////////// MAIN LOOP ///////////////////////////////

int main(void) {
	// use 80MHz
	SysCtlClockSet(
			SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL | SYSCTL_XTAL_16MHZ
					| SYSCTL_OSC_MAIN);

	initConsole();
	initCommandsUART();
	initPWM();

	while (true) {
		if (_validData) {
			int adjustedY = (int) ((_joystickY - 128) * 1.2);
			int adjustedX = (int) ((_joystickX - 128) * 0.75);
			setMotorA(adjustedY - adjustedX);
			setMotorB(adjustedY + adjustedX);

			UARTprintf("X: %d / Y: %d -- A: %d / B: %d\n", _joystickX,
					_joystickY, adjustedY - adjustedX, adjustedY + adjustedX);
		}

		SysCtlDelay(160000);
	}

	return 0;
}
