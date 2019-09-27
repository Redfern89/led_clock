#include <avr/io.h>
#include <util/delay.h>
#include "I2C.h"

void I2CInit(void) //I2C init timing sequence. details in datasheet
{
	SDAPORT&=(1<<SDA);
	SCLPORT&=(1<<SCL);

	I2C_SDA_HIGH;
	I2C_SCL_HIGH;

}

void I2CStart(void) //I2C start sequence. see datasheet
{
	I2C_SCL_HIGH;
	H_DEL;

	I2C_SDA_LOW;
	H_DEL;
}

void I2CStop(void)//I2C stop sequence. see datasheet
{
	I2C_SDA_LOW;
	H_DEL;
	I2C_SCL_HIGH;
	Q_DEL;
	I2C_SDA_HIGH;
	H_DEL;
}

uint8_t I2CWriteByte(uint8_t data) //I2C write byte sequence. see datasheet
{
	uint8_t i;

	for(i=0;i<8;i++)
	{
		I2C_SCL_LOW;
		Q_DEL;

		if(data & 0x80)
		I2C_SDA_HIGH;
		else
		I2C_SDA_LOW;

		H_DEL;

		I2C_SCL_HIGH;
		H_DEL;

		while((SCLPIN & (1<<SCL))==0);

		data=data<<1;
	}
	//The 9th clock (ACK Phase)
	I2C_SCL_LOW;
	Q_DEL;

	I2C_SDA_HIGH;
	H_DEL;

	I2C_SCL_HIGH;
	H_DEL;

	uint8_t ack=!(SDAPIN & (1<<SDA));

	I2C_SCL_LOW;
	H_DEL;

	return ack;
}


uint8_t I2CReadByte(uint8_t ack)//I2C read byte. see datasheet
{
	uint8_t data=0x00;
	uint8_t i;

	for(i=0;i<8;i++)
	{
		I2C_SCL_LOW;
		H_DEL;
		I2C_SCL_HIGH;
		H_DEL;

		while((SCLPIN & (1<<SCL))==0);

		if(SDAPIN &(1<<SDA))
		data|=(0x80>>i);
	}

	I2C_SCL_LOW;
	Q_DEL;						//Soft_I2C_Put_Ack

	if(ack)
	{
		I2C_SDA_LOW;
	}
	else
	{
		I2C_SDA_HIGH;
	}
	H_DEL;

	I2C_SCL_HIGH;
	H_DEL;

	I2C_SCL_LOW;
	H_DEL;

	return data;
}


