#include <avr/io.h>
#include <util/delay.h>
#include "I2C.h"
#include "DS1307.h"

/***************************************************

Function To Read Internal Registers of DS1307
---------------------------------------------

address : Address of the register
data: value of register is copied to this.


Returns:
0= Failure
1= Success
***************************************************/

uint8_t DS1307Read(uint8_t address,uint8_t *data)
{
	uint8_t res;   //result

	//Start

	I2CStart();

	//SLA+W (for dummy write to set register pointer)
	res = I2CWriteByte(DS1307_SLA_W); //DS1307 address + W

	//Error
	if(!res) return FALSE;

	//Now send the address of required register

	res = I2CWriteByte(address);

	//Error
	if(!res) return FALSE;

	//Repeat Start
	I2CStart();

	//SLA + R
	res = I2CWriteByte(DS1307_SLA_R); //DS1307 Address + R

	//Error
	if(!res) return FALSE;

	//Now read the value with NACK
	*data = I2CReadByte(0);

	//Error

	if(!res) return FALSE;

	//STOP
	I2CStop();

	return TRUE;
}

/***************************************************

Function To Write Internal Registers of DS1307

---------------------------------------------

address : Address of the register
data: value to write.


Returns:
0= Failure
1= Success
***************************************************/

uint8_t DS1307Write(uint8_t address,uint8_t data)
{
	uint8_t res;   //result

	//Start
	I2CStart();

	//SLA+W
	res = I2CWriteByte(DS1307_SLA_W); //DS1307 address + W

	//Error
	if(!res) return FALSE;

	//Now send the address of required register
	res = I2CWriteByte(address);

	//Error
	if(!res) return FALSE;

	//Now write the value

	res = I2CWriteByte(data);

	//Error
	if(!res) return FALSE;

	//STOP
	I2CStop();

	return TRUE;
}

uint8_t DS1307Init() {
	// Запускаем ход часов
	uint8_t temp;
	DS1307Read(0x00, &temp);
	temp &= ~(1 << 7); // обнуляем 7 бит
	DS1307Write(0x00, temp);
	
	return TRUE;
}