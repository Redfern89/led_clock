#define DS1307_SLA_W 0xD0 //Write address of the DS1307
#define DS1307_SLA_R 0xD1 //Read address

#define TRUE	1 //Define return values from Soft I2C functions
#define FALSE	0

extern uint8_t ee24cxx_Read(uint8_t, uint8_t*);
extern uint8_t ee24cxx_Write(uint8_t, uint8_t);

extern uint8_t DS1307Read(uint8_t,uint8_t*);
extern uint8_t DS1307Write(uint8_t,uint8_t);
extern uint8_t DS1307Init();
