#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include "I2C.h"
#include "I2C.c"
#include "DS1307.h"
#include "DS1307.c"

// Макросы для USART'а (использовал только при отладке и щас они нах ненужны)
#define FOSC 16000000L
#define BAUD 9600L
#define MYUBRR FOSC / 16 / BAUD - 1

// Настройки портов ввода-вывода
#define RCLK_DDR  DDRC
#define SCLK_DDR  DDRD
#define DATA_DDR  DDRC
#define RCLK_PORT PORTC
#define SCLK_PORT PORTD
#define DATA_PORT PORTC
#define RCLK    PC1
#define SCLK    PD5
#define DATA    PC0

// Упарвление защелкой и сдвигом регистров
#define _74hc595_RegisterLatch(code)  { RCLK_PORT &= ~(1 << RCLK); code; RCLK_PORT |= (1 << RCLK);  }
#define _74hc595_RegisterShift()    { SCLK_PORT &= ~(1 << SCLK); SCLK_PORT |= (1 << SCLK);    }
	
#define T2_START { TCCR2 = (1 << CS20) | (1 << CS21) | (0 << CS22); }
#define T2_STOP { TCCR2 = 0x00; }

unsigned char NC = 0xFF; // Отсутствующая преамбула
volatile unsigned char digits[11] = { 0xfc, 0x60, 0xda, 0xf2, 0x66, 0xb6, 0xbe, 0xe0, 0xfe, 0xf6 }; // Коды цифр на индикаторе
volatile uint16_t groups[12] = { 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10 }; // Группы светодиодов
volatile unsigned char display_data[12]  = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }; // Массив индикации
const int max_groups  = 12; // Минимальное колличество групп
volatile unsigned int display_pos = 0; // Текущее положение на дисплее
volatile unsigned int tmp = 0; // Пока ненадо
volatile unsigned long long int millis = 0; // Колличество миллисекунд с момента запуска
volatile int sqw_flag = 0; // Флаг прохождения одной секунды

// Таймер без delay
#define setInterval(n, tmr, code) { if ((millis - tmr) >= n) { tmr = millis; code; }}

//  ( это ааааадовый костыль, который, я надеюсь потом уберу )
// Переменные-таймеры/счетчики...так думаю, что нахуй они не нужны,
// ибо более одной задержки одновременно не планирую в программе ...
unsigned long long int tmr0 = 0;
unsigned long long int tmr1 = 0;
unsigned long long int tmr2 = 0;
unsigned long long int tmr3 = 0;

/* Протокол NEC */
const int NEC_MIN_CLK                   = 5;        // Минимальное значение, при котором следует начинать захват
volatile int NEC_REPEAT_FLAG            = 0;
volatile int NEC_START_FLAG             = 0;
volatile int NEC_IR_DONE                = 0;		// Флаг входящей команда с пульта
volatile unsigned long int NEC_SCLK     = 0;        // Тактовые синхроимпульсы (64 мкс)
volatile unsigned long int NEC_RECV_CNT = 0;        // Кол-во принятых битов
const static int NEC_MIN_HEADER_MESSAGE_CLK       = 190;      // Преамбула+пауза (минимальное время)
const static int NEC_MAX_HEADER_MESSAGE_CLK       = 245;      // Преамбула+пауза (максимальное время)
const static int NEC_MIN_REPEAT         = 80;
const static int NEC_MAX_REPEAT         = 170;
const int NEC_MIN_ONE_BIT_CLK = 30;
const int NEC_MAX_ONE_BIT_CLK = 40;
const int NEC_MIN_NUL_BIT_CLK = 15;
const int NEC_MAX_NUL_BIT_CLK = 25;
const static int NEC_MAX_RESET_OVF      = 1200; // Время сброса состояния приема
const static int NEC_PACKET_LENGTH      = 32; // Размер сообщения (без преамбулы)
volatile unsigned char addr1 = 0x00;	// Адрес
volatile unsigned char addr2 = 0x00;	// Инверсия адреса
volatile unsigned char cmd1 = 0x00;		// Команда
volatile unsigned char cmd2 = 0x00;		// Инверсия команды
volatile uint16_t command = 0x0000;		// Команда пульта
int mode = 0;

// Смещения для создания битовой маски сообщенй (адрес1, адрес2, команда1, команда2)
const int offset1_addr1 = 0;
const int offset2_addr1 = 9;
const int offset1_addr2 = 9;
const int offset2_addr2 = 17;
const int offset1_cmd1 = 17;
const int offset2_cmd1 = 25;
const int offset1_cmd2 = 25;
const int offset2_cmd2 = 33;

// Коды с пульта
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

// Структура DateTime (для удобства работы)
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

// Инициализация портов ввода-вывода SPI
void _74hc595_SPI_Init() {
	RCLK_DDR |= (1 << RCLK);
	SCLK_DDR |= (1 << SCLK);
	DATA_DDR |= (1 << DATA);
}

// Отправка данных в регистр
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

// Отправка данных в регистры с защелкиванием (2.0)
void _74hc595_RegisterWrite(uint16_t group, uint8_t segments) {
	_74hc595_RegisterLatch({
		_74hc595_SPI_send(group >> 8);
		_74hc595_SPI_send(group & 0xFF);
		_74hc595_SPI_send(segments);
	});
}

// Динамическая индикация
ISR (TIMER0_OVF_vect) {
	TCNT0 = 0xf8;

	/* Отсылаем текущие данные в сдвиговые регистры */
	display_pos = (display_pos + 1) % max_groups;
	_74hc595_RegisterWrite(groups[display_pos], display_data[display_pos]);
}

// Таймер подсчета миллисекунд
ISR (TIMER1_OVF_vect) {
	TCNT1 = 0xFF06;

	millis++;
	if (millis > 9223372036854775807) millis = 0; // НХУЯ СЕ!!!!
}

// Таймер подсчета промежутков между спадами на внешнем прерывании
ISR ( TIMER2_OVF_vect ) {
	TCNT2 = 0xF0;
	
	if (++NEC_SCLK >= NEC_MAX_RESET_OVF) {
		T2_STOP;
		NEC_SCLK = 0;
		NEC_START_FLAG = 0;
		NEC_REPEAT_FLAG = 0;
		command = 0x0000;
	}
	
	// Это не обязательно, но для перестраховки оставлю тут
	// Если в течении 1200 тиков пришло менее 32 бит, обнуляем счетчики и переходим в режим ожидания
	if (NEC_SCLK >= NEC_MAX_RESET_OVF && NEC_RECV_CNT < NEC_PACKET_LENGTH) {
		T2_STOP;
		NEC_SCLK = 0;
		NEC_RECV_CNT = 0;
		command = 0x0000;
	}
}

// Вход во внешнее прерывание
ISR (INT0_vect) {
	// Как только пришел первый импульс - начианем подсчет
	T2_START;
	if (NEC_SCLK > NEC_MIN_CLK) {
		// Определяем стартовое сообщение (преамбулу)
		if (NEC_SCLK >= NEC_MIN_HEADER_MESSAGE_CLK && NEC_SCLK < NEC_MAX_HEADER_MESSAGE_CLK) {
			NEC_START_FLAG = 1;
			NEC_REPEAT_FLAG = 0;
			NEC_RECV_CNT = 0;
		}
		
		if (NEC_SCLK >= NEC_MIN_REPEAT && NEC_SCLK < NEC_MAX_REPEAT && NEC_START_FLAG) {
			NEC_REPEAT_FLAG = 1;
		}
		
		/* Знаю, по идиотски, Но умнее лень было придумывать */
		
		// Тут определяем биты нулевого значения
		if ((NEC_SCLK >= NEC_MIN_NUL_BIT_CLK && NEC_SCLK < NEC_MAX_NUL_BIT_CLK) && NEC_START_FLAG) {
			NEC_RECV_CNT++; // Инкрементируем колличество принятых нулей
			// ну а тут мутнаые процедуры записи значений в переменные
			if (NEC_RECV_CNT >= offset1_addr1 && NEC_RECV_CNT < offset2_addr1) { // Если мы в диапозоне 1-8, ...
				addr1 &= ~(1 << (NEC_RECV_CNT - offset1_addr1)); // добавляем в addr1 нули в нужные места
			}
			// Остальные диапозоны тоже самое
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
		
		// Тут определяем биты положительного значения (такая-же хуйня как и с нулями, только интервалы у NEC_SCLK больше)
		if ((NEC_SCLK >= NEC_MIN_ONE_BIT_CLK && NEC_SCLK < NEC_MAX_ONE_BIT_CLK) && NEC_START_FLAG) {
			NEC_RECV_CNT++; // Инкрементируем колличество принятых едениц
			
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
		
		// Колличество нулей и едениц в конечном счете должно быть 32, на этом и остановимся
		if (NEC_RECV_CNT == NEC_PACKET_LENGTH) {
			// Выставляем в стартовое положение все счетчики и останавлиеваем подсчет
			NEC_RECV_CNT = 0;
			NEC_START_FLAG = 0;
			T2_STOP;

			// Проверка сообщения на целостность
			// В версии протокола 2.0 адрес имеет расширеное пространство и не имеет инверсии
			// Значит проверяем либо сложив обе инверсии (адреса и команды), либо только инверсии команды
			if ((((addr1 + addr2 == 0xFF) && (cmd1 + cmd2) == 0xFF)) || (cmd1 + cmd2 == 0xFF)) {
				NEC_IR_DONE = 1; // Сообщаем системе, что чтение завершено
				// Команду склеиваем с адресом (16 бит)
				command = ((addr1 + addr2) << 8) | cmd1;
			}
		}
		
	}
}

// Отображение цифр на дисплее
// Особо говорить тут нечего, тупо разнос числе на разряды. скука смертная
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

// Чтение данных из микросхемы
void DS1307_ReadDateTime( void ) {
	unsigned char temp;

	// Читаем данные и преобразуем из BCD в двоичную систему
	DS1307Read(0x00,&temp); // Чтение регистра секунд
	DateTime.Sec = (((temp & 0xF0) >> 4)*10)+(temp & 0x0F);

	DS1307Read(0x01,&temp); // Чтение регистра минут
	DateTime.Min = (((temp & 0xF0) >> 4)*10)+(temp & 0x0F);

	DS1307Read(0x02,&temp); // Чтение регистра часов
	DateTime.Hour = (((temp & 0xF0) >> 4)*10)+(temp & 0x0F);
}

// Функция пропорционально переносит значение (value) из текущего диапазона 
// значений (fromLow .. fromHigh) в новый диапазон (toLow .. toHigh), заданный параметрами
long map(long x, long in_min, long in_max, long out_min, long out_max) {
	return (x - in_min) * (out_max - out_min + 1) / (in_max - in_min + 1) + out_min;
}

// Процедура записи в микросхему из структуры TDateTime
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

// Вход во внешнее прерывание. DS1307 (SQW)
ISR ( INT1_vect ) {
	sqw_flag = 1;
}


// Код основной программы
int main(void) {
	asm("CLI"); // Запрещаем прерывания
	_delay_ms(100); // Мы же все тупимс утра? так и пустьмк потупит около 100мс. 
	
	// Инициализация таймера/счестчика 2 для динамической индикации
	TCCR0 |= (1 << CS02);
	TIMSK |= (1 << TOIE0);
	TCNT1 = 0;
	
	// Инициализация таймера/счестчика 1 для подсчета миллисекунд
	TCCR1B |= (0 << CS12) | (1 << CS11) | (1 << CS10);
	TIMSK |= (1 << TOIE1);
	TCNT1 = 0xFF06;
	
	// Инициализация таймера/счестчика 0 для подсчета времени во внешнем прерывании
	TCCR2 = (1 << CS20) | (1 << CS21) | (0 << CS22);
	TIMSK |= (1 << TOIE2);
	TCNT2 = 0xF0;
	
	// Инициализация внешнего прерывание INT0, INT1
	MCUCR |= (1 << ISC01) | (0 << ISC00);
	MCUCR |= (1 << ISC11) | (0 << ISC10);
	GICR |= (1 << INT0) | (1 << INT1);

	// Инициализация портов ввода-вывода SPI
	_74hc595_SPI_Init();
	
	asm("SEI"); // Разрешаем прерывания

	DS1307Init(); // Инициализация DS1307
	DS1307Write(0x07, 0b10010000); // Настраиваем выход SQW на частоту 1Hz

	// Переменные для работы интерфейса
	int menu_level = 0;	// позиция в главном меню
	int menu_flag = 0; // Говорит о том, что открыто меню в данный момент или нет
	int submenu_level = 0; // Позиция в подменю, клавиши на пульте: "<<" и ">>"
	int s_hour = 0, s_min = 0; // Настраиваемые значения
	int enter_menu_flag = 0; // Флаг входа в меню. выполняется каждый раз при входе в меню для приравнивания значений
	int exit_menu_flag = 0; // Флаг выхода из меню, для корректного завершения демок
	int blink_flag = 0; // Флаг мигания значний в настройках
	int pass_cnt = 4; // Счетчик колличества проходов по шкале
	int interval = 7; // Интервал перемещения по проходу
	int pass_end_flag = 0; // Флаг сброса прохода
	unsigned long long int scale = 0b1111111111111111111111111111111111111111111111111111111111111111; // Светодня шкала
	unsigned long long int demo[2] = {
		0b0101010101010101010101010101010101010101010101010101010101010101,
		0b1010101010101010101010101010101010101010101010101010101010101010
	};
	unsigned long long int exit_demo = 0b0111111111011111111001111111111000111111110001111111110000011111;
	int demo_interval = 0;	// Текущий проход в демо
	int demo_flag = 0; // Флаг входа в демо
	//int demo_count = 0;
	
	while (1) {
		// Рапределение значений по массиву
		/*
			Ввиду того, что я идиот неправильно спроектировал плату - приходтся страдать
			ибо первые три светодиода на шкале - это конец первой группы. а конец шкалы -
			это последние 5 светодиоов той-же первой группы. Кароче если идти от младшего
			бита к старшему (LSB) - нужно сдерунть со 2го по 7й и с 61 по 63 биты и сложить 
			их воедино. Да и все остальные группы вытаскивать их жопы. Так и живем
		*/
		display_data[5] = ((((scale >> 4) & 0b00011111) << 3) | (scale >> 61));
		display_data[10] = (unsigned char)(scale >> 53);
		display_data[2] = (unsigned char)(scale >> 45);
		display_data[1] = (unsigned char)(scale >> 37);
		display_data[7] = (unsigned char)(scale >> 29);
		display_data[0] = (unsigned char)(scale >> 21);
		display_data[3] = (unsigned char)(scale >> 13);
		display_data[4] = (unsigned char)((scale >> 9) << 4) | 0b00001111;
		
		// Читаем время из микросхемы
		DS1307_ReadDateTime();
		
		// Всякая муть с демо
		if (demo_flag) {
			setInterval(100, tmr1, {
				demo_interval++;
				if (demo_interval > 1) demo_interval = 0;
				scale = demo[demo_interval];
				display_data[6] = 0b01100011;
				display_data[8] = 0b00000011;
				display_data[9] = 0b10000101;
				display_data[11] = 0b01100001;
			});
		}

		if (menu_flag && !exit_menu_flag && !demo_flag) {
			setInterval(150, tmr1, {
				demo_interval++;
				if (demo_interval > 1) demo_interval = 0;
				scale = demo[demo_interval];
			});
		}
		
		if (exit_menu_flag) {
			scale = exit_demo;
			setInterval(1000, tmr1, {
				exit_menu_flag = 0;
				demo_interval = 0;
			});
		}
		
		// Вся суть анимации
		// Как я с этим заебался, пиздец. ...
		if (sqw_flag && !menu_flag && !exit_menu_flag && !demo_flag) { // Если прошла секунда
			interval = map(DateTime.Sec, 0, 59, 7, 30);
			setInterval(interval, tmr0, { // Устанавливаем интервал прохода
				pass_cnt++; // Приабавляем счетчик прохода
				// Если мы прошли больше, чем оставшееся время
				// PS не текущее, птому-что сдвиг идет от младшего к старшему биту и 
				// потому-что бегущий светодидод идет с канца шкалы к началу
				if (pass_cnt > (63 - (DateTime.Sec))) {
					pass_cnt = 0; // Обнуляем счетчик прохода
					sqw_flag = 0; // Обнуляем флаг SQW (т.е. секунда прошла)
					pass_end_flag = 1; // Выставляем флаг конца прохода шкалы
				}
				// Бежим по шкале одним светодиодом
				scale &= ~(1ULL << pass_cnt);
				// Что-бы не оставлять засобой шлейф из светодиодов, после,
				// Гасим остальные диоды за ним... т.е. биты выставляем в 1
				for (int h = 4; h <= (pass_cnt -1); h++) {
					scale |= (1ULL << h);
				}
			});
		}
		
		// Если-же мы прошли шкалу
		if (pass_end_flag && !menu_flag && !exit_menu_flag && !demo_flag) {
			// Рисуем уже пройденную шкалу от начала
			for (int j = 59; j >= (59 - DateTime.Sec); j--) {
				scale &= ~(1ULL << (j + 4));
			}
			// Выставляем флаг конца прохода шкалы в ноль
			pass_end_flag = 0;
		}
		
		// Если пришла команда с пульа
		if (NEC_IR_DONE) {
			// Уже нет
			NEC_IR_DONE = 0;
			
			// Демо-режим
			if (command == TEST && !menu_flag) {
				demo_flag = 1;
			}
			
			// Заходим в меню
			if (command == MENU && !demo_flag) {
				menu_level++; // Прибавляем уровень меню
				menu_flag = 1; // Говорим системе, что мы в меню
				submenu_level = 0; // Обнуляем позицию подменю
				if (menu_level > 2) menu_level = 1; // Пунктов меню всего два
				enter_menu_flag = 1; // Говорим системе, что вошли в меню
			}
			// Если мы в меню и нажали на RETURN
			if (command == RETURN && menu_flag) {
				// Выходим из меню и обнуляем счетчики позиций
				menu_flag = 0;
				menu_level = 0;
				submenu_level = 0;
				exit_menu_flag = 1;
				demo_interval = 0;
			}
			// Выходим из demo
			if (command == RETURN && demo_flag) {
				demo_flag = 0;
			}
			// При нажатии на ">>" перемещаемся по подменю
			if (command == FWND && menu_flag) {
				submenu_level++;
				if (submenu_level > 1) submenu_level = 0;
			}
			// Тоже самое для "<<"
			if (command == RWND && menu_flag) {
				submenu_level--;
				if (submenu_level < 0) submenu_level = 1;
			}
			// Если мы в меню и нажали на "+"
			if (command == PLUS && menu_flag) {
				if (submenu_level == 0) { // Если настраиваем часы
					s_hour++; // прибавляем часы
					if (s_hour > 23) s_hour = 0; // но не больше 23
					print_display(s_hour, s_min, 0, 0, 0); // так как настраиваемые цифры мигают, задержим их во время настройки
				}
				// тоже самое и с настройками минут
				if (submenu_level == 1) {
					s_min++;
					if (s_min > 59) s_min = 0;
					print_display(s_hour, s_min, 0, 0, 0);
				}
			}
			// Тоже самое, что и в "+", только значения убавляем и соблюдаем пороги
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
			// Кнопка PLAY - это кнопка OK, при ее нажатии сохраняем текущие настройки
			if (command == PLAY && menu_flag) {
				if (menu_level == 1) { // Если настраиваем время
					DateTime.Sec = 0; // Устанавливаем такой формат XX:XX:00
					DateTime.Min = s_min;
					DateTime.Hour = s_hour;
					DS1307_WriteDateTime(); // Пишем новое время в микросхему DS1307
				}
				if (menu_level == 2) { // Если настраиваем будильник
				//	AlarmData.Hour = s_hour;
				//	AlarmData.Min = s_min;
				//	AlarmWriteTime(); // Записываем настройки в EEPROM
				}
				// И выходим из меню после сохранения всех настроек
				menu_flag = 0;
				menu_level = 0;
				submenu_level = 0;
			}

		}
		
		// Если м в меню
		if (menu_flag) {
			if (enter_menu_flag) {
				enter_menu_flag = 0;
				if (menu_level == 1) { // Если настраиваем время
					DS1307_ReadDateTime();
					s_hour = DateTime.Hour;
					s_min = DateTime.Min;
				}
			}
			// Мигание настраиваемых значений
			setInterval(450, tmr0, {
				blink_flag ^= 1;
				if (submenu_level == 0) { // Настариваем часы
					print_display(s_hour, s_min, 0, blink_flag, 0);
				}
				if (submenu_level == 1) { // Настариваем минуты
					print_display(s_hour, s_min, 0, 0, blink_flag);
				}
			});

		} else {
			if (!demo_flag) {
				// Тупо отображаем время
				print_display(DateTime.Hour, DateTime.Min, (DateTime.Sec % 2), 0, 0);
			}
		}
		

	}
	
	return 1;
}
