#ifndef RTU_PINS_H_
#define RTU_PINS_H_

#define PP_ENABLE_ETHERNET		1
#define PP_ENABLE_ADS131M08		1

#define PP_ENABLE_PULSE_INPUTS	1
#define PP_ENABLE_RELAY			1
#define PP_ENABLE_HEARTBEAT		1

#define PP_ENABLE_RS485			1
#define PP_ENABLE_MODBUS		1
#define PP_ENABLE_MODBUS_TCP_S	1
#define PP_ENABLE_MODBUS_MASTER	0
#define PP_ENABLE_MODBUS_SLAVE	1

// Ethernet SPI host selection
// 0 = share SPI2_HOST with ADC (original pins GPIO11/13/12)
// 1 = use dedicated SPI3_HOST (MOSI=GPIO35, MISO=GPIO36, CLK=GPIO37)
#define PP_ETH_USE_SPI3			1

#endif // RTU_PINS_H_
