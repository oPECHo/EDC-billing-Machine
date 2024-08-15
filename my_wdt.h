#include <avr/wdt.h>

void WDT_interrupt_enable(uint8_t timeout_v)
{
	unsigned char bakSREG;
	uint8_t prescaler;
	
	prescaler = timeout_v & 0x07;
	prescaler |= (1<<WDIE);
	if (timeout_v > 7)
		prescaler |= (1<<WDP3);		
		
	bakSREG = SREG;	
	cli();
	wdt_reset();
	WDTCSR |= ( (1<<WDCE) | (1<<WDE) ); 
	WDTCSR = prescaler;
	SREG = bakSREG;
}


void WDT__enable(uint8_t timeout_v)
{
	unsigned char bakSREG;
	uint8_t prescaler;
	
	prescaler = timeout_v & 0x07;
	prescaler |= (1<<WDE);
	if (timeout_v > 7)
		prescaler |= (1<<WDP3);		
		
	bakSREG = SREG;	
	cli();
	wdt_reset();
	WDTCSR |= ( (1<<WDCE) | (1<<WDE) ); 
	WDTCSR = prescaler;
	
	SREG = bakSREG;
}

void WDT__disable(void)
{
	uint8_t bakSREG;
	bakSREG = SREG;
	cli();
	wdt_reset();
	MCUSR &= ~(1<<WDRF);  //?????????????? WDRF ????????????
	WDTCSR |= ( (1<<WDCE) | (1<<WDE) ); 
	WDTCSR = (0<<WDE) |(0<<WDIE);   	
	SREG = bakSREG;
}


#define wdt_timeout_15ms	0	//ค่าเวลารอ 15.625	มิลลิวินาที
#define wdt_timeout_31ms	1	//ค่าเวลารอ	31.25	มิลลิวินาที
#define wdt_timeout_62ms	2	//ค่าเวลารอ	62.5	มิลลิวินาที
#define wdt_timeout_125ms	3	//ค่าเวลารอ	0.125	วินาที
#define wdt_timeout_250ms	4	//ค่าเวลารอ	0.25	วินาที
#define wdt_timeout_500ms	5	//ค่าเวลารอ	0.5	วินาที
#define wdt_timeout_1sec	6	//ค่าเวลารอ	1.0	วินาที
#define wdt_timeout_2sec	7	//ค่าเวลารอ	2.0	วินาที
#define wdt_timeout_4sec	8	//ค่าเวลารอ	4.0	วินาที
#define wdt_timeout_8sec	9	//ค่าเวลารอ	8.0	วินาที