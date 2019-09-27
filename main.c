#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "I2C.h"
#include "I2C.c"
#include "DS1307.h"
#include "DS1307.c"

// ������� ��� USART'� (����������� ������ ��� ������� � ��� ��� ��� �������)
#define FOSC 16000000L
#define BAUD 9600L
#define MYUBRR FOSC / 16 / BAUD - 1

// ��������� ������ �����-������
#define RCLK_DDR  DDRC
#define SCLK_DDR  DDRD
#define DATA_DDR  DDRC
#define RCLK_PORT PORTC
#define SCLK_PORT PORTD
#define DATA_PORT PORTC
#define RCLK    PC1
#define SCLK    PD5
#define DATA    PC0

// ���������� �������� � ������� ���������
#define _74hc595_RegisterLatch(code)  { RCLK_PORT &= ~(1 << RCLK); code; RCLK_PORT |= (1 << RCLK);  }
#define _74hc595_RegisterShift()    { SCLK_PORT &= ~(1 << SCLK); SCLK_PORT |= (1 << SCLK);    }
	
#define T2_START { TCCR2 = (1 << CS20) | (1 << CS21) | (0 << CS22); }
#define T2_STOP { TCCR2 = 0x00; }

unsigned char NC = 0xFF;
volatile unsigned char digits[11] = { 0xfc, 0x60, 0xda, 0xf2, 0x66, 0xb6, 0xbe, 0xe0, 0xfe, 0xf6 };
volatile uint16_t groups[12] = { 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10 };
volatile unsigned char display_data[12]  = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const int max_groups  = 12;
volatile unsigned int display_pos = 0;
volatile unsigned int tmp = 0;
volatile unsigned long long int millis = 0;
volatile int sqw_flag = 0;

// ������ ��� delay
#define setInterval(n, tmr, code) { if ((millis - tmr) >= n) { tmr = millis; code; }}

//  ( ��� ���������� �������, �������, � ������� ����� ����� )
// ����������-�������/��������...��� �����, ��� ����� ��� �� �����,
// ��� ����� ����� �������� ������������ �� �������� � ��������� ...
unsigned long long int tmr0 = 0;
unsigned long long int tmr1 = 0;
unsigned long long int tmr2 = 0;
unsigned long long int tmr3 = 0;

/* �������� NEC */
const int NEC_MIN_CLK                   = 5;        // ����������� ��������, ��� ������� ������� �������� ������
volatile int NEC_REPEAT_FLAG            = 0;
volatile int NEC_START_FLAG             = 0;
volatile int NEC_IR_DONE                = 0;
volatile unsigned long int NEC_SCLK     = 0;        // �������� �������������� (64 ���)
volatile unsigned long int NEC_RECV_CNT = 0;        // ���-�� �������� �����
const static int NEC_MIN_HEADER_MESSAGE_CLK       = 190;      // ���������+����� (����������� �����)
const static int NEC_MAX_HEADER_MESSAGE_CLK       = 245;      // ���������+����� (������������ �����)
const static int NEC_MIN_REPEAT         = 80;
const static int NEC_MAX_REPEAT         = 170;
const int NEC_MIN_ONE_BIT_CLK = 30;
const int NEC_MAX_ONE_BIT_CLK = 40;
const int NEC_MIN_NUL_BIT_CLK = 15;
const int NEC_MAX_NUL_BIT_CLK = 25;
const static int NEC_MAX_RESET_OVF      = 1200;
const static int NEC_PACKET_LENGTH      = 32;
volatile unsigned char addr1 = 0x00;	// �����
volatile unsigned char addr2 = 0x00;	// �������� ������
volatile unsigned char cmd1 = 0x00;		// �������
volatile unsigned char cmd2 = 0x00;		// �������� �������
volatile uint16_t command = 0x0000;		// ������� ������
int mode = 0;

// �������� ��� �������� ������� ����� �������� (�����1, �����2, �������1, �������2)
const int offset1_addr1 = 0;
const int offset2_addr1 = 9;
const int offset1_addr2 = 9;
const int offset2_addr2 = 17;
const int offset1_cmd1 = 17;
const int offset2_cmd1 = 25;
const int offset1_cmd2 = 25;
const int offset2_cmd2 = 33;

// ���� � ������
const uint16_t POWER_OFF = 0xFF45;
const uint16_t MENU	= 0xFF47;
const uint16_t TEST = 0xFF44;
const uint16_t PLUS = 0xFF40;
const uint16_t RETURN = 0xFF43;
const uint16_t RWND = 0xFF07;
const uint16_t PLAY = 0xFF15;
const uint16_t FWND = 0xFF09;
const uint16_t MINUS = 0xFF19;
const uint16_t CLEAR = 0xFF0D;
const uint16_t D0 = 0xFF16;
const uint16_t D1 = 0xFF0C;
const uint16_t D2 = 0xFF18;
const uint16_t D3 = 0xFF5E;
const uint16_t D4 = 0xFF08;
const uint16_t D5 = 0xFF1C;
const uint16_t D6 = 0xFF5A;
const uint16_t D7 = 0xFF42;
const uint16_t D8 = 0xFF52;
const uint16_t D9 = 0xFF4A;

// ��������� DateTime (��� �������� ������)
typedef struct {
	int Sec;
	int Min;
	int Hour;
	int Month;
	int Day;
	int Year;
	int WeekDay;
	int AMPM;
	int H12_24;
} TDateTime;
TDateTime DateTime;

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
								USART functions
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
void USORT_Init(unsigned char ubrr) {
	UBRRH = (unsigned char)(ubrr >> 8);
	UBRRL = (unsigned char)ubrr;
	UCSRB = (1 << RXEN) | (1 << TXEN);
	UCSRC = (1 << USBS) | (3 << UCSZ0) | (1 << URSEL);
}

void USORT_Transmit( unsigned char data ) {
	while ( !( UCSRA & (1 << UDRE)) );
	UDR = data;
}


// ������������� ������ �����-������ SPI
void _74hc595_SPI_Init() {
	RCLK_DDR |= (1 << RCLK);
	SCLK_DDR |= (1 << SCLK);
	DATA_DDR |= (1 << DATA);
}

// �������� ������ � �������
void _74hc595_SPI_send(char data) {
	int i;
	unsigned char val;
	
	for (i = 0; i < 8; i++) {
		val = !!(data & (1 << (7 - i)));
		if (val) DATA_PORT |= 1 << DATA;
		else DATA_PORT &= ~(1 << DATA);
		_74hc595_RegisterShift();
	}
}

// �������� ������ � �������� � ������������� (2.0)
void _74hc595_RegisterWrite(uint16_t group, uint8_t segments) {
	_74hc595_RegisterLatch({
		_74hc595_SPI_send(group >> 8);
		_74hc595_SPI_send(group & 0xFF);
		_74hc595_SPI_send(segments);
	});
}

// ������������ ���������
ISR (TIMER0_OVF_vect) {
	TCNT0 = 0xf8;

	/* �������� ������� ������ � ��������� �������� */
	display_pos = (display_pos + 1) % max_groups;
	_74hc595_RegisterWrite(groups[display_pos], display_data[display_pos]);
}

// ������ �������� �����������
ISR (TIMER1_OVF_vect) {
	TCNT1 = 0xFF06;

	millis++;
}

// ������ �������� ����������� ����� ������� �� ������� ����������
ISR ( TIMER2_OVF_vect ) {
	TCNT2 = 0xF0;
	
	if (++NEC_SCLK >= NEC_MAX_RESET_OVF) {
		T2_STOP;
		NEC_SCLK = 0;
		NEC_START_FLAG = 0;
		NEC_REPEAT_FLAG = 0;
		command = 0x0000;
	}
	
	// ��� �� �����������, �� ��� ������������� ������� ���
	// ���� � ������� 1200 ����� ������ ����� 32 ���, �������� �������� � ��������� � ����� ��������
	if (NEC_SCLK >= NEC_MAX_RESET_OVF && NEC_RECV_CNT < NEC_PACKET_LENGTH) {
		T2_STOP;
		NEC_SCLK = 0;
		NEC_RECV_CNT = 0;
		command = 0x0000;
	}
}

// ������� ���������� �� IRDA-���������
ISR (INT0_vect) {
	T2_START;
	if (NEC_SCLK > NEC_MIN_CLK) {
		// ��� ���������� ��������� ��������� (���������)
		if (NEC_SCLK >= NEC_MIN_HEADER_MESSAGE_CLK && NEC_SCLK < NEC_MAX_HEADER_MESSAGE_CLK) {
			NEC_START_FLAG = 1;
			NEC_REPEAT_FLAG = 0;
			NEC_RECV_CNT = 0;
		}
		
		if (NEC_SCLK >= NEC_MIN_REPEAT && NEC_SCLK < NEC_MAX_REPEAT && NEC_START_FLAG) {
			NEC_REPEAT_FLAG = 1;
		}
		
		/* ����, �� ��������, �� ����� ���� ���� ����������� */
		
		// ��� ���������� ���� �������� ��������
		if ((NEC_SCLK >= NEC_MIN_NUL_BIT_CLK && NEC_SCLK < NEC_MAX_NUL_BIT_CLK) && NEC_START_FLAG) {
			NEC_RECV_CNT++; // �������������� ����������� �������� �����
			// �� � ��� ������� ��������� ������ �������� � ����������
			if (NEC_RECV_CNT >= offset1_addr1 && NEC_RECV_CNT < offset2_addr1) { // ���� �� � ��������� 1-8, ...
				addr1 &= ~(1 << (NEC_RECV_CNT - offset1_addr1)); // ��������� � addr1 ���� � ������ �����
			}
			// ��������� ��������� ���� �����
			if (NEC_RECV_CNT >= offset1_addr2 && NEC_RECV_CNT < offset2_addr2) {
				addr2 &= ~(1 << (NEC_RECV_CNT - offset1_addr2));
			}
			if (NEC_RECV_CNT >= offset1_cmd1 && NEC_RECV_CNT < offset2_cmd1) {
				cmd1 &= ~(1 << (NEC_RECV_CNT - offset1_cmd1));
			}
			if (NEC_RECV_CNT >= offset1_cmd2 && NEC_RECV_CNT < offset2_cmd2) {
				cmd2 &= ~(1 << (NEC_RECV_CNT - offset1_cmd2));
			}
		//	IRActiveLed(0);
			
		}
		
		// ��� ���������� ���� �������������� �������� (�����-�� ����� ��� � � ������, ������ ��������� � NEC_SCLK ������)
		if ((NEC_SCLK >= NEC_MIN_ONE_BIT_CLK && NEC_SCLK < NEC_MAX_ONE_BIT_CLK) && NEC_START_FLAG) {
			NEC_RECV_CNT++; // �������������� ����������� �������� ������
			
			if (NEC_RECV_CNT >= offset1_addr1 && NEC_RECV_CNT < offset2_addr1) {
				addr1 |= (1 << (NEC_RECV_CNT - offset1_addr1));
			}
			if (NEC_RECV_CNT >= offset1_addr2 && NEC_RECV_CNT < offset2_addr2) {
				addr2 |= (1 << (NEC_RECV_CNT - offset1_addr2));
			}
			if (NEC_RECV_CNT >= offset1_cmd1 && NEC_RECV_CNT < offset2_cmd1) {
				cmd1 |= (1 << (NEC_RECV_CNT - offset1_cmd1));
			}
			if (NEC_RECV_CNT >= offset1_cmd2 && NEC_RECV_CNT < offset2_cmd2) {
				cmd2 |= (1 << (NEC_RECV_CNT - offset1_cmd2));
			}
		//	IRActiveLed(1);
		}
		
		NEC_SCLK = 0;
		
		// ����������� ����� � ������ � �������� ����� ������ ���� 32, �� ���� � �����������
		if (NEC_RECV_CNT == NEC_PACKET_LENGTH) {
			// ���������� � ��������� ��������� ��� �������� � �������������� �������
			NEC_RECV_CNT = 0;
			NEC_START_FLAG = 0;
			T2_STOP;

			// �������� ��������� �� �����������
			// � ������ ��������� 2.0 ����� ����� ���������� ������������ � �� ����� ��������
			// ������ ��������� ���� ������ ��� �������� (������ � �������), ���� ������ �������� �������
			if ((((addr1 + addr2 == 0xFF) && (cmd1 + cmd2) == 0xFF)) || (cmd1 + cmd2 == 0xFF)) {
				NEC_IR_DONE = 1; // �������� �������, ��� ������ ���������
				// ������� ��������� � ������� (16 ���)
				command = ((addr1 + addr2) << 8) | cmd1;
			}
		}
		
	}
}

// ����������� ���� �� �������
int print_display(int hours, int mins, int bl, int hsbl, int mbl) {
	if (!hsbl) {
		if (hours == 0) {
				display_data[6] = ~digits[0];
				display_data[8] = ~digits[0];
			} else if (hours >= 0 && hours <= 9) {
				display_data[6] = ~digits[0];
				display_data[8] = ~digits[hours];
			} else if (hours >= 10 && hours <= 99) {
				display_data[6] = ~digits[(unsigned int)round(hours % 100) / 10];
				display_data[8] = ~digits[(unsigned int)round(hours % 10)];
			}
			if (bl) {
				display_data[8] &= ~(1 << 0);
			}
	} else {
		display_data[6] = 0xff;
		display_data[8] = 0xff;
	}
	
	if (!mbl) {
		if (mins == 0) {
			display_data[9] = ~digits[0];
			display_data[11] = ~digits[0];
		} else if (mins >= 0 && mins <= 9) {
			display_data[9] = ~digits[0];
			display_data[11] = ~digits[mins];
		} else if (mins >= 10 && mins <= 99) {
			display_data[9] = ~digits[(unsigned int)round(mins % 100) / 10];
			display_data[11] = ~digits[(unsigned int)round(mins % 10)];
		}
	} else {
		display_data[9] = 0xff;
		display_data[11] = 0xff;
	}
	
	return 1;
}

void DS1307_ReadDateTime( void ) {
	unsigned char temp;

	// ������ ������ � ����������� �� BCD � �������� �������
	DS1307Read(0x00,&temp); // ������ �������� ������
	DateTime.Sec = (((temp & 0xF0) >> 4)*10)+(temp & 0x0F);

	DS1307Read(0x01,&temp); // ������ �������� �����
	DateTime.Min = (((temp & 0xF0) >> 4)*10)+(temp & 0x0F);

	DS1307Read(0x02,&temp); // ������ �������� �����
	DateTime.Hour = (((temp & 0xF0) >> 4)*10)+(temp & 0x0F);
}

// ������� ��������������� ��������� �������� (value) �� �������� ��������� 
// �������� (fromLow .. fromHigh) � ����� �������� (toLow .. toHigh), �������� �����������
// ������ �������� ���� � ARDUINO
long map(long x, long in_min, long in_max, long out_min, long out_max) {
	return (x - in_min) * (out_max - out_min + 1) / (in_max - in_min + 1) + out_min;
}

// ��������� ������ � ���������� �� ��������� TDateTime
void DS1307_WriteDateTime() {
	unsigned char tmp;
	tmp = ((DateTime.Sec / 10) << 4) | (DateTime.Sec % 10);
	DS1307Write(0x00, tmp);
	tmp = ((DateTime.Min / 10) << 4) | (DateTime.Min % 10);
	DS1307Write(0x01, tmp);
	tmp = ((DateTime.Hour / 10) << 4) | (DateTime.Hour % 10);
	DS1307Write(0x02, tmp);
	tmp = ((DateTime.WeekDay / 10) << 4) | (DateTime.WeekDay % 10);
	DS1307Write(0x03, tmp);
}

// ������� ���������� � DS1307 (SQW)
ISR ( INT1_vect ) {
	sqw_flag = 1;
}


// ��� �������� ���������
int main(void) {
	asm("CLI");
	_delay_ms(100);
	
	// ������������� �������/��������� 2 ��� ������������ ���������
	TCCR0 |= (1 << CS02);
	TIMSK |= (1 << TOIE0);
	TCNT1 = 0;
	
	// ������ ��� �������� �����������
	TCCR1B |= (0 << CS12) | (1 << CS11) | (1 << CS10);
	TIMSK |= (1 << TOIE1);
	TCNT1 = 0xFF06;
	
	// ������ ��� ������ � NEC-���������� IRDA
	TCCR2 = (1 << CS20) | (1 << CS21) | (0 << CS22);
	TIMSK |= (1 << TOIE2);
	TCNT2 = 0xF0;
	
	// ������� ����������
	MCUCR |= (1 << ISC01) | (0 << ISC00);
	MCUCR |= (1 << ISC11) | (0 << ISC10);
	GICR |= (1 << INT0) | (1 << INT1);

	// ������������� ������ �����-������ SPI
	_74hc595_SPI_Init();
	asm("SEI");

	DS1307Init(); // ������������� DS1307
	DS1307Write(0x07, 0b10010000); // ����������� ����� SQW �� ������� 1Hz

	// ���������� ��� ������ ����������
	int menu_level = 0;	// ������� � ������� ����
	int menu_flag = 0; // ������� � ���, ��� ������� ���� � ������ ������ ��� ���
	int submenu_level = 0; // ������� � �������, ������� �� ������: "<<" � ">>"
	int s_hour = 0, s_min = 0; // ������������� ��������
	int enter_menu_flag = 0; // ���� ����� � ����. ����������� ������ ��� ��� ����� � ���� ��� ������������� ��������
	int exit_menu_flag = 0; // ���� ������ �� ����, ��� ����������� ���������� �����
	int blink_flag = 0; // ���� ������� ������� � ����������
	int pass_cnt = 4; // ������� ����������� �������� �� �����
	int interval = 7; // �������� ����������� �� �������
	int pass_end_flag = 0; // ���� ������ �������
	unsigned long long int scale = 0b1111111111111111111111111111111111111111111111111111111111111111; // �������� �����
	unsigned long long int demo[2] = {
		0b0101010101010101010101010101010101010101010101010101010101010101,
		0b1010101010101010101010101010101010101010101010101010101010101010
	};
//	unsigned long long int demo2[2] = {
//		0b0000111100001111000011110000111100001111000011110000111100001111,
//		0b1111000011110000111100001111000011110000111100001111000011110000
//	};
	int demo_pass = 0;
	int demo_flag = 0;
	int demo_count = 0;
	int demo_interval = 30;
	int mode = 0 ;
	int sec = 0;
	
	
	while (1) {
		// ������������ �������� �� �������
		// ���, ��� � ���� ��������� � �������������� ������ �� �����, ����� �������
		display_data[5] = ((((scale >> 4) & 0b00011111) << 3) | (scale >> 61)); // ����� ������ 5 ��� � ��������� 3 ���� � ����� � ���� ����
		display_data[10] = (unsigned char)(scale >> 53);
		display_data[2] = (unsigned char)(scale >> 45);
		display_data[1] = (unsigned char)(scale >> 37);
		display_data[7] = (unsigned char)(scale >> 29);
		display_data[0] = (unsigned char)(scale >> 21);
		display_data[3] = (unsigned char)(scale >> 13);
		display_data[4] = (unsigned char)((scale >> 9) << 4) | 0b00001111; // ��������� ������ ���� �� 4� �����������, ������ ��������� ���, ��� ����� 4�
		
		// ������ ����� �� ����������
		DS1307_ReadDateTime();
		
		if (mode == 0) {
			sec = DateTime.Sec;
		} else {
			sec = map(DateTime.Hour, 0, 12, 0, 59);
		}
		
		if (demo_flag) {
			if (demo_count == 0) {
				setInterval(demo_interval, tmr1, {
					demo_pass++;
					if (demo_pass > 1) demo_pass = 0;
					scale = demo[demo_pass];
					display_data[6] = 0b01100011;
					display_data[8] = 0b00000011;
					display_data[9] = 0b10000101;
					display_data[11] = 0b01100001;
				});
			}
			if (demo_count == 1) {
				setInterval(demo_interval, tmr1, {
					demo_pass++;
					if (demo_pass > 1) demo_pass = 0;
					scale = demo[demo_pass];
					display_data[6] = 0b01100011;
					display_data[8] = 0b00000011;
					display_data[9] = 0b10000101;
					display_data[11] = 0b01100001;
				});
			}
			//if (demo_count == 3) {
				//setInterval(demo_interval, tmr1, {
					//demo_pass++;
					//if (demo_pass > 1) demo_pass = 0;
					//scale = demo3[demo_pass];
					//display_data[6] = 0b01100011;
					//display_data[8] = 0b00000011;
					//display_data[9] = 0b10000101;
					//display_data[11] = 0b01100001;
				//});
			//}
		}

		if (menu_flag && !exit_menu_flag && !demo_flag) {
			setInterval(500, tmr1, {
				demo_pass++;
				if (demo_pass > 1) demo_pass = 0;
				scale = demo[demo_pass];
			});
		}
		
		if (exit_menu_flag) {
			setInterval(1000, tmr1, {
				exit_menu_flag = 0;
				demo_pass = 0;
			});
		}
		
		// ��� ���� ��������
		if (sqw_flag && !menu_flag && !exit_menu_flag && !demo_flag) { // ���� ������ �������
			interval = map(sec, 0, 59, 7, 30);
			// ��� � � ���� ��������, ������
			setInterval(interval, tmr0, { // ������������� �������� �������
				pass_cnt++; // ����������� ������� �������
				// ���� �� ������ ������, ��� ���������� �����
				// PS �� �������, �����-��� ����� ���� �� �������� � �������� ���� � 
				// ������-��� ������� ���������� ���� � ����� ����� � ������
				if (pass_cnt > (63 - (sec))) {
					pass_cnt = 0; // �������� ������� �������
					sqw_flag = 0; // �������� ���� SQW (�.�. ������� ������)
					pass_end_flag = 1; // ���������� ���� ����� ������� �����
				}
				// ����� �� ����� ����� �����������
				scale &= ~(1ULL << pass_cnt);
				// ���-�� �� ��������� ������� ����� �� �����������, �����,
				// ����� ��������� ����� �� ���... �.�. ���� ���������� � 1
				for (int h = 4; h <= (pass_cnt -1); h++) {
					scale |= (1ULL << h);
				}
			});
		}
		
		// ����-�� �� ������ �����
		if (pass_end_flag && !menu_flag && !exit_menu_flag && !demo_flag) {
			// ������ ��� ���������� ����� �� ������
			for (int j = 59; j >= (59 - sec); j--) {
				scale &= ~(1ULL << (j + 4));
			}
			// ���������� ���� ����� ������� ����� � ����
			pass_end_flag = 0;
		}
		
		// ���� ������ ������� � �����
		if (NEC_IR_DONE) {
			// ��� ���
			NEC_IR_DONE = 0;
			
			if (command == CLEAR) {
				mode++;
				if (mode > 1) mode = 1;
			}
			
			// ����-�����
			if (command == TEST && !menu_flag) {
				demo_flag = 1;
			}
			
			// ������� � ����
			if (command == MENU && !demo_flag) {
				menu_level++; // ���������� ������� ����
				menu_flag = 1; // ������� �������, ��� �� � ����
				submenu_level = 0; // �������� ������� �������
				if (menu_level > 2) menu_level = 1; // ������� ���� ����� ���
				enter_menu_flag = 1; // ������� �������, ��� ����� � ����
			}
			// ���� �� � ���� � ������ �� RETURN
			if (command == RETURN && menu_flag) {
				// ������� �� ���� � �������� �������� �������
				menu_flag = 0;
				menu_level = 0;
				submenu_level = 0;
				exit_menu_flag = 1;
				demo_pass = 0;
			}
			// ������� �� demo
			if (command == RETURN && demo_flag) {
				demo_flag = 0;
			}
			// ��� ������� �� ">>" ������������ �� �������
			if (command == FWND && menu_flag) {
				submenu_level++;
				if (submenu_level > 1) submenu_level = 0;
			}
			// ���� ����� ��� "<<"
			if (command == RWND && menu_flag) {
				submenu_level--;
				if (submenu_level < 0) submenu_level = 1;
			}
			
			// ����������� ����+
			if (command == FWND && demo_flag) {
				demo_count++;
				if (demo_count > 1) demo_count = 0;
			}
			if (command == RWND && demo_flag) {
				demo_count--;
				if (demo_count < 0) demo_count = 1;
			}
			if (command == PLUS && demo_flag) {
				demo_interval = demo_interval + 10;
				if (demo_interval > 600) demo_interval = 10;
				//print_display()
			}
			if (command == MINUS && demo_flag) {
				demo_interval = demo_interval - 10;
				if (demo_interval < 10) demo_interval = 600;
			}
			// ���� �� � ���� � ������ �� "+"
			if (command == PLUS && menu_flag) {
				if (submenu_level == 0) { // ���� ����������� ����
					s_hour++; // ���������� ����
					if (s_hour > 23) s_hour = 0; // �� �� ������ 23
					print_display(s_hour, s_min, 0, 0, 0); // ��� ��� ������������� ����� ������, �������� �� �� ����� ���������
				}
				// ���� ����� � � ����������� �����
				if (submenu_level == 1) {
					s_min++;
					if (s_min > 59) s_min = 0;
					print_display(s_hour, s_min, 0, 0, 0);
				}
			}
			// ���� �����, ��� � � "+", ������ �������� �������� � ��������� ������
			if (command == MINUS && menu_flag) {
				if (submenu_level == 0) {
					s_hour--;
					if (s_hour < 0) s_hour = 23;
					print_display(s_hour, s_min, 0, 0, 0);
				}
				if (submenu_level == 1) {
					s_min--;
					if (s_min < 0) s_min = 59;
					print_display(s_hour, s_min, 0, 0, 0);
				}
			}
			// ������ PLAY - ��� ������ OK, ��� �� ������� ��������� ������� ���������
			if (command == PLAY && menu_flag) {
				if (menu_level == 1) { // ���� ����������� �����
					DateTime.Sec = 0; // ������������� ����� ������ XX:XX:00
					DateTime.Min = s_min;
					DateTime.Hour = s_hour;
					DS1307_WriteDateTime(); // ����� ����� ����� � ���������� DS1307
				}
				if (menu_level == 2) { // ���� ����������� ���������
				//	AlarmData.Hour = s_hour;
				//	AlarmData.Min = s_min;
				//	AlarmWriteTime(); // ���������� ��������� � EEPROM
				}
				// � ������� �� ���� ����� ���������� ���� ��������
				menu_flag = 0;
				menu_level = 0;
				submenu_level = 0;
			}

		}

		if (menu_flag) {
			if (enter_menu_flag) {
				enter_menu_flag = 0;
				if (menu_level == 1) { // ���� ����������� �����
					DS1307_ReadDateTime();
					s_hour = DateTime.Hour;
					s_min = DateTime.Min;
				}
			}
			setInterval(450, tmr0, {
				blink_flag ^= 1;
				if (submenu_level == 0) {
					print_display(s_hour, s_min, 0, blink_flag, 0);
				}
				if (submenu_level == 1) { 
					print_display(s_hour, s_min, 0, 0, blink_flag);
				}
			});

		} else {
			if (!demo_flag) {
				if (mode == 0) {
					print_display(DateTime.Hour, DateTime.Min, (DateTime.Sec % 2), 0, 0);
				} else {
					print_display(DateTime.Min, DateTime.Sec, (DateTime.Sec % 2), 0, 0);
				}
			}
		}
		

	}
	
	return 1;
}
