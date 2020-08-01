/*****************************************************************************

The goal of this program is to bit-bang a selectable 50- or 60-Hz PSU tick
signal replacement for Amiga 1000, 2000 and 3000 mainboards. It does not rely
on any additional libraries, so is developed to target the STM8S105 MCUs.
Ymmv with other models.

QA Measures:
- Just my own smoke testing on a breadboarded STM8S105K4T6C.
- Confirmed output looked reasonable on the scope at startup and after running
for 30 mins. (Did this a couple times.)
- Programmed with ST-Link2 SWIM, compiled to 229 bytes wtih sdcc.

The signals will have a 50% duty cycle.
50- and 60-Hz selection can be made by holding PB1 low or high, respectively.

I am not sure about the voltage requirements or max load for the Amiga 2000
(and other) mainboard ticks, so additional level adjustment may be necessary
and some form of current limiting is highly advised.

Notes on deriving the clocks follow:

Let F0 -> base clock frequency
Let N50 -> number of cycles in one 50-Hz period 
Let N60 -> number of cycles in one 60-Hz period

Then,
N[50,60] = F0 / [50,60]

Let F0 = 16MHz
Then,

N50
 = 16MHz / 50
 = 320,000

N60
 = 16MHz / 60
 = 266,666 + 2/3
 = 266,667 [- 1/3]  (ie. choose to overshoot by 1/48M-s)

Since the timer's counter is only 16-bit, dividing the base clock
sufficiently can keep an entire tick period within the clock's
range. So,

Let F0 = 2MHz
Then,

N50
 = 2MHz / 50
 = 40,000

N60
 = 2MHz / 60
 = 33,333 [+ 1/3]  (ie. we will undershoot by 1/6 millionths of a second)

Timer 1 will be configured for a 2MHz clock, and the program will reset this
clock every 40K or 33,333 cycles to line up with the 50Hz or 60Hz periods,
respectively.

I am choosing to ignore any additional drift that may result due to naive
implementation of the clock reset.

*****************************************************************************/

#include <stdint.h>

#define CLK_CKDIVR *(volatile unsigned char *)0x50C6
#define TIM1_CNTRH *(volatile unsigned char *)0x525E
#define TIM1_CNTRL *(volatile unsigned char *)0x525F
#define TIM1_PSCRH *(volatile unsigned char *)0x5260
#define TIM1_PSCRL *(volatile unsigned char *)0x5261
#define TIM1_CR1 *(volatile unsigned char *)0x5250
#define PB_ODR *(volatile unsigned char *)0x5005
#define PB_IDR *(volatile unsigned char *)0x5006
#define PB_DDR *(volatile unsigned char *)0x5007
#define PB_CR1 *(volatile unsigned char *)0x5008

const unsigned short TICK_PIN = 0x01;
const unsigned short FREQ_SELECT_PIN = 0x02;
const unsigned short TICKS_50HZ = 40000;
const unsigned short TICKS_60HZ = 33333; // undershoots by 1/3rd of a tick

// Read (clock) and write (set_clock) the timer 1 (50Hz) & 2 (60Hz) registers.
unsigned short clock(void) {
  unsigned char h = TIM1_CNTRH;
  unsigned char l = TIM1_CNTRL;
  return ((unsigned short)(h) << 8 | l);
}

void reset_clock() {
  // Just force the clock to the end of its range so it resets immediately.
  //
  // TODO: Is it possible to auto-reset at a given value?
  //
  TIM1_CNTRH = 0xff;
  TIM1_CNTRL = 0xff;
}

void main(void) {
  // Set HSIDIV and CPUDIV for full 16MHz operation (no prescaling)
  CLK_CKDIVR = 0x00;

  // Set TIM1+2 prescalers for a 2MHz clock (PSCR) and enable the timer (CR1)
  // by setting the TIM1 port 1 enable bit (0x1).
  // 16MHz / 8 = 2MHz
  // Since the prescaler value is defined as PSCR+1 (so that 0 is divide by 1),
  // we must take care to subtract 1 from whatever we intend to set. (Ex. So
  // setting PSCR=7 really means divide by 7+1=8.)
  TIM1_PSCRH = 0x00;
  TIM1_PSCRL = 0x08 - 1; 
  TIM1_CR1 = 0x01;

  // Configure tick and freq selct pins for i/o (data direction register DDR) pushpull
  // (configuration register CR1)
  //
  // TODO: Are these the best options for I/O?
  //
  PB_DDR = TICK_PIN | FREQ_SELECT_PIN;
  PB_CR1 = TICK_PIN | FREQ_SELECT_PIN;

  // Always start tick low
  PB_ODR &= ~TICK_PIN;

  while (1)
  {
    const unsigned short ticks = (PB_IDR & FREQ_SELECT_PIN ? TICKS_60HZ : TICKS_50HZ);
    const unsigned short ck0 = clock();

    unsigned int ck = ck0;
    if (ck0 > ticks) {
      reset_clock();
      ck = 0;
    }

    if (ck > ticks/2) {
      PB_ODR |= TICK_PIN;
    } else {
      PB_ODR &= ~TICK_PIN;
    }
  }
}
