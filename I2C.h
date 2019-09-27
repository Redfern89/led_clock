#define SCLPORT	PORTC	//TAKE PORTD as SCL OUTPUT WRITE
#define SCLDDR	DDRC	//TAKE DDRB as SCL INPUT/OUTPUT configure

#define SDAPORT	PORTC	//TAKE PORTD as SDA OUTPUT WRITE
#define SDADDR	DDRC	//TAKE PORTD as SDA INPUT configure

#define SDAPIN	PINC	//TAKE PORTD TO READ DATA
#define SCLPIN	PINC	//TAKE PORTD TO READ DATA

#define SCL	PC3		//PORTb.0 PIN AS SCL PIN
#define SDA	PC2		//PORTb.3 PIN AS SDA PIN

#define I2C_SDA_LOW	    SDADDR|=((1<<SDA)) //Macros to toggle the ports
#define I2C_SDA_HIGH	SDADDR&=(~(1<<SDA))

#define I2C_SCL_LOW	    SCLDDR|=((1<<SCL))
#define I2C_SCL_HIGH	SCLDDR&=(~(1<<SCL))

#define Q_DEL _delay_loop_2(3) //Some delay functions
#define H_DEL _delay_loop_2(5)

extern void I2CInit(void);
extern void I2CStart(void);
extern void I2CStop(void);
extern uint8_t I2CWriteByte(uint8_t);
extern uint8_t I2CReadByte(uint8_t);

