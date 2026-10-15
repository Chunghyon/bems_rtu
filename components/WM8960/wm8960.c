#include "math.h"
#include "wm8960.h"
#include "hh_parameters.h"

static const char *TAG = "Codec";
uint8_t speaker_volume = 121; // 47~127, 47=mute, 48=-73dB, ..., 127=+6dB

// The WM8960 does not support I2C reads
// This means we must keep a local copy of all the register values
// We will instantiate with default values
// As we write to the device, we will also make sure
// To update our local copy as well, stored here in this array.
// Each register is 9-bits, so we will store them as a uint16_t
// They are in order from R0-R55, and we even keep blank spots for the
// "reserved" registers. This way we can use the register address macro 
// defines above to easiy access each local copy of each register.
// Example: _registerLocalCopy[WM8960_REG_LEFT_INPUT_VOLUME]
uint16_t _registerLocalCopy[56] = {
	0x0097, // R0 (0x00)
	0x0097, // R1 (0x01)
	0x0000, // R2 (0x02)
	0x0000, // R3 (0x03)
	0x0000, // R4 (0x04)
	0x0008, // F5 (0x05)
	0x0000, // R6 (0x06)
	0x000A, // R7 (0x07)
	0x01C0, // R8 (0x08)
	0x0000, // R9 (0x09)
	0x00FF, // R10 (0x0a)
	0x00FF, // R11 (0x0b)
	0x0000, // R12 (0x0C) RESERVED
	0x0000, // R13 (0x0D) RESERVED
	0x0000, // R14 (0x0E) RESERVED
	0x0000, // R15 (0x0F) RESERVED
	0x0000, // R16 (0x10)
	0x007B, // R17 (0x11)
	0x0100, // R18 (0x12)
	0x0032, // R19 (0x13)
	0x0000, // R20 (0x14)
	0x00C3, // R21 (0x15)
	0x00C3, // R22 (0x16)
	0x01C0, // R23 (0x17)
	0x0000, // R24 (0x18)
	0x0000, // R25 (0x19)
	0x0000, // R26 (0x1A)
	0x0000, // R27 (0x1B)
	0x0000, // R28 (0x1C)
	0x0000, // R29 (0x1D)
	0x0000, // R30 (0x1E) RESERVED
	0x0000, // R31 (0x1F) RESERVED
	0x0100, // R32 (0x20)
	0x0100, // R33 (0x21)
	0x0050, // R34 (0x22)
	0x0000, // R35 (0x23) RESERVED
	0x0000, // R36 (0x24) RESERVED
	0x0050, // R37 (0x25)
	0x0000, // R38 (0x26)
	0x0000, // R39 (0x27)
	0x0000, // R40 (0x28)
	0x0000, // R41 (0x29)
	0x0040, // R42 (0x2A)
	0x0000, // R43 (0x2B)
	0x0000, // R44 (0x2C)
	0x0050, // R45 (0x2D)
	0x0050, // R46 (0x2E)
	0x0000, // R47 (0x2F)
	0x0002, // R48 (0x30)
	0x0037, // R49 (0x31)
	0x0000, // R50 (0x32) RESERVED
	0x0080, // R51 (0x33)
	0x0008, // R52 (0x34)
	0x0031, // R53 (0x35)
	0x0026, // R54 (0x36)
	0x00e9, // R55 (0x37)
};

const uint16_t _registerDefaults[56] = {
	0x0097, // R0 (0x00)
	0x0097, // R1 (0x01)
	0x0000, // R2 (0x02)
	0x0000, // R3 (0x03)
	0x0000, // R4 (0x04)
	0x0008, // F5 (0x05)
	0x0000, // R6 (0x06)
	0x000A, // R7 (0x07)
	0x01C0, // R8 (0x08)
	0x0000, // R9 (0x09)
	0x00FF, // R10 (0x0a)
	0x00FF, // R11 (0x0b)
	0x0000, // R12 (0x0C) RESERVED
	0x0000, // R13 (0x0D) RESERVED
	0x0000, // R14 (0x0E) RESERVED
	0x0000, // R15 (0x0F) RESERVED
	0x0000, // R16 (0x10)
	0x007B, // R17 (0x11)
	0x0100, // R18 (0x12)
	0x0032, // R19 (0x13)
	0x0000, // R20 (0x14)
	0x00C3, // R21 (0x15)
	0x00C3, // R22 (0x16)
	0x01C0, // R23 (0x17)
	0x0000, // R24 (0x18)
	0x0000, // R25 (0x19)
	0x0000, // R26 (0x1A)
	0x0000, // R27 (0x1B)
	0x0000, // R28 (0x1C)
	0x0000, // R29 (0x1D)
	0x0000, // R30 (0x1E) RESERVED
	0x0000, // R31 (0x1F) RESERVED
	0x0100, // R32 (0x20)
	0x0100, // R33 (0x21)
	0x0050, // R34 (0x22)
	0x0000, // R35 (0x23) RESERVED
	0x0000, // R36 (0x24) RESERVED
	0x0050, // R37 (0x25)
	0x0000, // R38 (0x26)
	0x0000, // R39 (0x27)
	0x0000, // R40 (0x28)
	0x0000, // R41 (0x29)
	0x0040, // R42 (0x2A)
	0x0000, // R43 (0x2B)
	0x0000, // R44 (0x2C)
	0x0050, // R45 (0x2D)
	0x0050, // R46 (0x2E)
	0x0000, // R47 (0x2F)
	0x0002, // R48 (0x30)
	0x0037, // R49 (0x31)
	0x0000, // R50 (0x32) RESERVED
	0x0080, // R51 (0x33)
	0x0008, // R52 (0x34)
	0x0031, // R53 (0x35)
	0x0026, // R54 (0x36)
	0x00e9, // R55 (0x37)
};

static esp_err_t i2c_trans(i2c_port_t i2c_num, uint8_t addr, void *txdata, uint8_t txlen)
{
	esp_err_t rc;
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();

	rc = i2c_master_start(cmd);
	if(ESP_OK!=rc) ESP_LOGE(TAG, "[W] Master_start: %s", esp_err_to_name(rc));
	rc = i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
	if(ESP_OK!=rc) ESP_LOGE(TAG, "[W] Master_write_byte: %s", esp_err_to_name(rc));
	rc = i2c_master_write(cmd, txdata, txlen, ACK_CHECK_EN);
	if(ESP_OK!=rc) ESP_LOGE(TAG, "[W] Master_write: %s", esp_err_to_name(rc));
	rc = i2c_master_stop(cmd);
	if(ESP_OK!=rc) ESP_LOGE(TAG, "[W] Master stop: %s", esp_err_to_name(rc));
	rc = i2c_master_cmd_begin(I2C_BUS_NO, cmd, 10);
	if(ESP_OK!=rc) ESP_LOGE(TAG, "[W] CMD begin: %s", esp_err_to_name(rc));
	i2c_cmd_link_delete(cmd);
	return rc;
}

static bool wm8960_isConnected(void)
{
	esp_err_t rc;
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	rc = i2c_master_start(cmd);
	if (rc != ESP_OK) ESP_LOGE(TAG, "[W] Master_start: %s", esp_err_to_name(rc));
	rc = i2c_master_write_byte(cmd, (CODEC_ADDR << 1) | I2C_MASTER_WRITE, true); // ACK_CHECK_EN
	if (rc != ESP_OK) ESP_LOGE(TAG, "[W] Master_write_byte: %s", esp_err_to_name(rc));
	rc = i2c_master_stop(cmd);
	if (rc != ESP_OK) ESP_LOGE(TAG, "[W] Master stop: %s", esp_err_to_name(rc));
	rc = i2c_master_cmd_begin(I2C_BUS_NO, cmd, pdMS_TO_TICKS(100));
	if (rc != ESP_OK) ESP_LOGE(TAG, "[W] CMD begin: %s", esp_err_to_name(rc));
	i2c_cmd_link_delete(cmd);
	return (rc==ESP_OK); // ESP_OK면 연결됨, 아니면 연결 안됨
}

// writeRegister(uint8_t reg, uint16_t value)
// General-purpose write to a register
// Returns 1 if successful, 0 if something failed (I2C error)
// The WM8960 has 9 bit registers.
// To write a register, we must do the following
// Send 3 bytes
// Byte0 = device address + read/write bit
// Control_byte_1 = register-to-write address (7-bits) plus 9th bit of data
// Control_byte_2 = remaining 8 bits of register data
static esp_err_t wm8960_writeRegister(uint8_t addr, uint16_t value)
{
	esp_err_t rc;
	uint8_t buff[3];
	buff[0] = (addr << 1) | ((value >> 8) & 0x0f);
	buff[1] = value & 0xff;
	rc = i2c_trans(I2C_BUS_NO, CODEC_ADDR, buff, 2);
	if(rc != ESP_OK) ESP_LOGE(TAG, "wm8960_writeRegister %03xh@%02xh error: %s", value, addr, esp_err_to_name(rc));
	return rc;

}

// writeRegisterBit
// Writes a 0 or 1 to the desired bit in the desired register
static bool wm8960_writeRegisterBit(uint8_t registerAddress, uint8_t bitNumber, bool bitValue)
{
	// Get the local copy of the register
	uint16_t regvalue = _registerLocalCopy[registerAddress]; 

	if(bitValue == 1) 
	{
		regvalue |= (1<<bitNumber); // Set only the bit we want	
	}
	else {
		regvalue &= ~(1<<bitNumber); // Clear only the bit we want	
	}

	// Write modified value to device
	// If successful, update local copy
	if (wm8960_writeRegister(registerAddress, regvalue) == ESP_OK) 
	{
		_registerLocalCopy[registerAddress] = regvalue;
		return true;
	}
	else {
		ESP_LOGE(TAG, "wm8960_writeRegisterBit error: R%02xh not set to %d\n", registerAddress, bitValue);
	}
	return false;
}

// writeRegisterMultiBits
// This function writes data into the desired bits within the desired register
// Some settings require more than just flipping a single bit within a register.
// For these settings use this more advanced register write helper function.
// 
// For example, to change the LIN2BOOST setting to +6dB,
// I need to write a setting of 7 (aka +6dB) to the bits [3:1] in the 
// WM8960_REG_INPUT_BOOST_MIXER_1 register. Like so...
// _writeRegisterMultiBits(WM8960_REG_INPUT_BOOST_MIXER_1, 3, 1, 7);
static bool wm8960_writeRegisterMultiBits(uint8_t registerAddress, uint8_t settingMsbNum, uint8_t settingLsbNum, uint8_t setting)
{
	uint8_t numOfBits = (settingMsbNum - settingLsbNum) + 1;

	// Get the local copy of the register
	uint16_t regvalue = _registerLocalCopy[registerAddress]; 

	for(int i = 0 ; i < numOfBits ; i++)
	{
		regvalue &= ~(1 << (settingLsbNum + i)); // Clear bits we care about 
	}

	// Shift and set the bits from in incoming desired setting value
	regvalue |= (setting << settingLsbNum); 

	// Write modified value to device
	// If successful, update local copy
	if (wm8960_writeRegister(registerAddress, regvalue) == ESP_OK) 
	{
		_registerLocalCopy[registerAddress] = regvalue;
		return true;
	}
	else {
		ESP_LOGE(TAG, "wm8960_writeRegisterMultiBits error: R%02xh not set to %xh\n", registerAddress, setting);
	}
	return false;
}

static uint16_t wm8960_readRegisterMultiBits(uint8_t registerAddress, uint8_t settingMsbNum, uint8_t settingLsbNum)
{
	uint8_t numOfBits = (settingMsbNum - settingLsbNum) + 1;

	// Get the local copy of the register
	uint16_t regvalue = _registerLocalCopy[registerAddress]; 

	regvalue = regvalue >> settingLsbNum; // Shift down to LSB

	uint16_t mask = (1 << numOfBits) - 1; // Create a mask for the number of bits we care about

	regvalue &= mask; // Clear all other bits

	return regvalue;
}

// enableVREF
// Necessary for all other functions of the CODEC
// VREF is a single bit we can flip in Register 25 (19h), WM8960_REG_PWR_MGMT_1
// VREF is bit 6, 0 = power down, 1 = power up
// Returns 1 if successful, 0 if something failed (I2C error)
bool wm8960_enableVREF(void)
{ 
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 6, 1);
}

// disableVREF
// Use this to save power
// VREF is a single bit we can flip in Register 25 (19h), WM8960_REG_PWR_MGMT_1
// VREF is bit 6, 0 = power down, 1 = power up
// Returns 1 if successful, 0 if something failed (I2C error)
bool wm8960_disableVREF(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 6, 0);
}

// reset
// Use this to reset all registers to their default state
// Note, this can also be done by cycling power to the device
// Returns 1 if successful, 0 if something failed (I2C error)
bool wm8960_reset(void)
{
	// Doesn't matter which bit we flip, writing anything will cause the reset
	if (wm8960_writeRegisterBit(WM8960_REG_RESET, 7, 1)) 
	{
		// Update our local copy of the registers to reflect the reset
		for(int i = 0 ; i < 56 ; i++)
		{
			_registerLocalCopy[i] = _registerDefaults[i]; 
		}
		return true;
	}
	return false;
}

bool wm8960_showRegister(void)
{
	CC_LOGNI("WM8960 Registers:\n");
	for(int i = 0 ; i < 56 ; i++)
	{
		CC_LOGNI("R%02d[%02xh]: 0x%03X\n", i, i, _registerLocalCopy[i]);
	}
	return true;
}

bool wm8960_enableAINL(void)
{ 
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 5, 1);
}

bool wm8960_disableAINL(void)
{ 
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 5, 0);
}

bool wm8960_enableAINR(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 4, 1);
}

bool wm8960_disableAINR(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 4, 0);
}

bool wm8960_enableLMIC(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 5, 1);
}

bool wm8960_disableLMIC(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 5, 0);
}

bool wm8960_enableRMIC(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 4, 1);
}

bool wm8960_disableRMIC(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 4, 0);
}

bool wm8960_enableLMICBOOST(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 5, 1);
}

bool wm8960_disableLMICBOOST(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 5, 0);
}

bool wm8960_enableRMICBOOST(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 4, 1);
}

bool wm8960_disableRMICBOOST(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 4, 0);
}

// PGA input signal select
// Each PGA (left and right) has a switch on its non-inverting input.
// On PGA_LEFT:
// 	*You can select between VMID, LINPUT2 or LINPUT3
// 	*Note, the inverting input of PGA_LEFT is perminantly connected to LINPUT1
// On PGA_RIGHT:
//	*You can select between VMIN, RINPUT2 or RINPUT3
// 	*Note, the inverting input of PGA_RIGHT is perminantly connected to RINPUT1

// 3 options: WM8960_PGAL_LINPUT2, WM8960_PGAL_LINPUT3, WM8960_PGAL_VMID
bool wm8960_pgaLeftNonInvSignalSelect(uint8_t signal)
{
	// Clear LMP2 and LMP3
	// Necessary because the previous setting could have either set,
	// And we don't want to confuse the wm8960_
	// Only 1 input can be selected.

	// LMP3
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 7, 0); 

	// LMP2
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 6, 0); 
	bool result3 = false;

	if(signal == WM8960_PGAL_LINPUT2)
	{
	// LMP2
	result3 = wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 6, 1); 
	}
	else if(signal == WM8960_PGAL_LINPUT3)
	{
	// LMP3
	result3 = wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 7, 1); 
	}
	else if(signal == WM8960_PGAL_VMID)
	{
	// Don't set any bits. When both LMP2 and LMP3 are cleared, then the signal 
	// is set to VMID
	}
	return (result1 && result2 && result3);
}

 // 3 options: WM8960_PGAR_RINPUT2, WM8960_PGAR_RINPUT3, WM8960_PGAR_VMID
bool wm8960_pgaRightNonInvSignalSelect(uint8_t signal)
{
	// Clear RMP2 and RMP3
	// Necessary because the previous setting could have either set,
	// And we don't want to confuse the wm8960_
	// Only 1 input can be selected.

	// RMP3
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 7, 0); 

	// RMP2
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 6, 0); 
	bool result3 = false;

	if(signal == WM8960_PGAR_RINPUT2)
	{
	// RMP2
	result3 = wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 6, 1); 
	}
	else if(signal == WM8960_PGAR_RINPUT3)
	{
	// RMP3
	result3 = wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 7, 1); 
	}
	else if(signal == WM8960_PGAR_VMID)
	{
	// Don't set any bits. When both RMP2 and RMP3 are cleared, then the signal 
	// is set to VMID
	}
	return (result1 && result2 && result3);
}

// Connection from each INPUT1 to the inverting input of its PGA
bool wm8960_connectLMN1(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 8, 1);
}

// Disconnect LINPUT1 to inverting input of Left Input PGA
bool wm8960_disconnectLMN1(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 8, 0);
}

// Connect RINPUT1 from inverting input of Right Input PGA
bool wm8960_connectRMN1(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 8, 1);
}

// Disconnect RINPUT1 to inverting input of Right Input PGA
bool wm8960_disconnectRMN1(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 8, 0);
}

// Connections from output of PGAs to downstream "boost mixers".

// Connect Left Input PGA to Left Input Boost mixer
bool wm8960_connectLMIC2B(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 3, 1);
}

// Disconnect Left Input PGA to Left Input Boost mixer
bool wm8960_disconnectLMIC2B(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCL_SIGNAL_PATH, 3, 0);
}

// Connect Right Input PGA to Right Input Boost mixer
bool wm8960_connectRMIC2B(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 3, 1);
}

// Disconnect Right Input PGA to Right Input Boost mixer
bool wm8960_disconnectRMIC2B(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADCR_SIGNAL_PATH, 3, 0);
}

// 0-63, (0 = -17.25dB) <<-- 0.75dB steps -->> (63 = +30dB)
bool wm8960_setLINVOL(uint8_t volume) 
{
	if(volume > 63) volume = 63; // Limit incoming values max
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_LEFT_INPUT_VOLUME,5,0,volume);
	bool result2 = wm8960_pgaLeftIPVUSet();
	return (result1 && result2);
}

// setLINVOLDB
// Sets the volume of the PGA input buffer amp to a specified dB value 
// passed in as a float argument.
// Valid dB settings are -17.25 up to +30.00
// -17.25 = -17.25dB (MIN)
// ... 0.75dB steps ...
// 30.00 = +30.00dB	(MAX)
bool wm8960_setLINVOLDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setLINVOL()
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_PGA_GAIN_OFFSET, WM8960_PGA_GAIN_STEPSIZE, WM8960_PGA_GAIN_MIN, WM8960_PGA_GAIN_MAX);

	return wm8960_setLINVOL(volume);
}

// 0-63, (0 = -17.25dB) <<-- 0.75dB steps -->> (63 = +30dB)
bool wm8960_setRINVOL(uint8_t volume) 
{
	if(volume > 63) volume = 63; // Limit incoming values max
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_RIGHT_INPUT_VOLUME,5,0,volume);
	bool result2 = wm8960_pgaRightIPVUSet();
	return (result1 && result2);
}

// setRINVOLDB
// Sets the volume of the PGA input buffer amp to a specified dB value 
// passed in as a float argument.
// Valid dB settings are -17.25 up to +30.00
// -17.25 = -17.25dB (MIN)
// ... 0.75dB steps ...
// 30.00 = +30.00dB	(MAX)
bool wm8960_setRINVOLDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setRINVOL()
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_PGA_GAIN_OFFSET, WM8960_PGA_GAIN_STEPSIZE, WM8960_PGA_GAIN_MIN, WM8960_PGA_GAIN_MAX);

	return wm8960_setRINVOL(volume);
}

// Zero Cross prevents zipper sounds on volume changes
// Sets both left and right PGAs
bool wm8960_enablePgaZeroCross(void)
{
	if (wm8960_writeRegisterBit(WM8960_REG_LEFT_INPUT_VOLUME, 6, 1) == 0) return false;
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_INPUT_VOLUME, 6, 1);
}

bool wm8960_disablePgaZeroCross(void)
{
	if (wm8960_writeRegisterBit(WM8960_REG_LEFT_INPUT_VOLUME, 6, 0) == 0) return false;
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_INPUT_VOLUME, 6, 0);
}

bool wm8960_enableLINMUTE(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_INPUT_VOLUME, 7, 1);
}

bool wm8960_disableLINMUTE(void)
{
	wm8960_writeRegisterBit(WM8960_REG_LEFT_INPUT_VOLUME, 7, 0);
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_INPUT_VOLUME, 8, 1);
}

bool wm8960_enableRINMUTE(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_INPUT_VOLUME, 7, 1);
}

bool wm8960_disableRINMUTE(void)
{
	wm8960_writeRegisterBit(WM8960_REG_RIGHT_INPUT_VOLUME, 7, 0);
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_INPUT_VOLUME, 8, 1);
}

// Causes left and right input PGA volumes to be updated (LINVOL and RINVOL)
bool wm8960_pgaLeftIPVUSet(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_INPUT_VOLUME, 8, 1);
}

 // Causes left and right input PGA volumes to be updated (LINVOL and RINVOL)
bool wm8960_pgaRightIPVUSet(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_INPUT_VOLUME, 8, 1);
}


// Input Boosts

// 0-3, 0 = +0dB, 1 = +13dB, 2 = +20dB, 3 = +29dB
bool wm8960_setLMICBOOST(uint8_t boost_gain) 
{
	if(boost_gain > 3) boost_gain = 3; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ADCL_SIGNAL_PATH,5,4,boost_gain);
}

// 0-3, 0 = +0dB, 1 = +13dB, 2 = +20dB, 3 = +29dB
bool wm8960_setRMICBOOST(uint8_t boost_gain) 
{
	if(boost_gain > 3) boost_gain = 3; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ADCR_SIGNAL_PATH,5,4,boost_gain);
}

// 0-7, 0 = Mute, 1 = -12dB ... 3dB steps ... 7 = +6dB
bool wm8960_setLIN3BOOST(uint8_t boost_gain) 
{
	if(boost_gain > 7) boost_gain = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_INPUT_BOOST_MIXER_1,6,4,boost_gain);
}

// 0-7, 0 = Mute, 1 = -12dB ... 3dB steps ... 7 = +6dB
bool wm8960_setLIN2BOOST(uint8_t boost_gain) 
{
	if(boost_gain > 7) boost_gain = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_INPUT_BOOST_MIXER_1,3,1,boost_gain);
}

// 0-7, 0 = Mute, 1 = -12dB ... 3dB steps ... 7 = +6dB
bool wm8960_setRIN3BOOST(uint8_t boost_gain) 
{
	if(boost_gain > 7) boost_gain = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_INPUT_BOOST_MIXER_2,6,4,boost_gain);
}

// 0-7, 0 = Mute, 1 = -12dB ... 3dB steps ... 7 = +6dB	
bool wm8960_setRIN2BOOST(uint8_t boost_gain) 
{
	if(boost_gain > 7) boost_gain = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_INPUT_BOOST_MIXER_2,3,1,boost_gain);
}

// Mic Bias control
bool wm8960_enableMicBias(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 1, 1);
}

bool wm8960_disableMicBias(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 1, 0);
}

// WM8960_MIC_BIAS_VOLTAGE_0_9_AVDD (0.9*AVDD) 
// or WM8960_MIC_BIAS_VOLTAGE_0_65_AVDD (0.65*AVDD)
bool wm8960_setMicBiasVoltage(bool voltage)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 0, voltage);
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// ADC
/////////////////////////////////////////////////////////

bool wm8960_enableAdcLeft(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 3, 1);
}

bool wm8960_disableAdcLeft(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 3, 0);
}

bool wm8960_enableAdcRight(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 2, 1);
}

bool wm8960_disableAdcRight(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_1, 2, 0);
}

// ADC digital volume
// Note, also needs to handle control of the ADCVU bits (volume update).
// Valid inputs are 0-255
// 0 = mute
// 1 = -97dB
// ... 0.5dB steps up to
// 195 = +0dB
// 255 = +30dB

bool wm8960_setAdcLeftDigitalVolume(uint8_t volume)
{
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_LEFT_ADC_VOLUME,7,0,volume);
	bool result2 = wm8960_adcLeftADCVUSet();
	return (result1 && result2);
}
bool wm8960_setAdcRightDigitalVolume(uint8_t volume)
{
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_RIGHT_ADC_VOLUME,7,0,volume);
	bool result2 = wm8960_adcRightADCVUSet();
	return (result1 && result2);
}

// Causes left and right input adc digital volumes to be updated
bool wm8960_adcLeftADCVUSet(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_ADC_VOLUME, 8, 1);
}

// Causes left and right input adc digital volumes to be updated
bool wm8960_adcRightADCVUSet(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_ADC_VOLUME, 8, 1);
}

// ADC digital volume DB
// Sets the volume of the ADC to a specified dB value passed in as a float 
// argument.
// Valid dB settings are -97.00 up to +30.0 (0.5dB steps)
// -97.50 (or lower) = MUTE
// -97.00 = -97.00dB (MIN)
// ... 0.5dB steps ...
// 30.00 = +30.00dB	(MAX)

bool wm8960_setAdcLeftDigitalVolumeDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setAdcLeftDigitalVolume()
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_ADC_GAIN_OFFSET, WM8960_ADC_GAIN_STEPSIZE, WM8960_ADC_GAIN_MIN, WM8960_ADC_GAIN_MAX);

	return wm8960_setAdcLeftDigitalVolume(volume);
}
bool wm8960_setAdcRightDigitalVolumeDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setAdcRightDigitalVolume()
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_ADC_GAIN_OFFSET, WM8960_ADC_GAIN_STEPSIZE, WM8960_ADC_GAIN_MIN, WM8960_ADC_GAIN_MAX);

	return wm8960_setAdcRightDigitalVolume(volume);
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// ALC
/////////////////////////////////////////////////////////

// Automatic Level Control
// Note that when the ALC function is enabled, the settings of registers 0 and 
// 1 (LINVOL, IPVU, LIZC, LINMUTE, RINVOL, RIZC and RINMUTE) are ignored.
bool wm8960_enableAlc(uint8_t mode)
{
	bool bit8 = (mode>>1);
	bool bit7 = (mode & 0b00000001);
	if (wm8960_writeRegisterBit(WM8960_REG_ALC1, 8, bit8) == 0) return false;
	return wm8960_writeRegisterBit(WM8960_REG_ALC1, 7, bit7);
}

 // Also sets alc sample rate to match global sample rate.
bool wm8960_disableAlc(void)
{
	if (wm8960_writeRegisterBit(WM8960_REG_ALC1, 8, 0) == 0) return false;
	return wm8960_writeRegisterBit(WM8960_REG_ALC1, 7, 0);
}

// Valid inputs are 0-15, 0 = -22.5dB FS, ... 1.5dB steps ... , 15 = -1.5dB FS
bool wm8960_setAlcTarget(uint8_t target) 
{
	if(target > 15) target = 15; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ALC1,3,0,target);
}

// Valid inputs are 0-10, 0 = 24ms, 1 = 48ms, ... 10 = 24.58seconds
bool wm8960_setAlcDecay(uint8_t decay) 
{
	if(decay > 10) decay = 10; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ALC3,7,4,decay);
}

// Valid inputs are 0-10, 0 = 6ms, 1 = 12ms, 2 = 24ms, ... 10 = 6.14seconds
bool wm8960_setAlcAttack(uint8_t attack) 
{
	if(attack > 10) attack = 10; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ALC3,3,0,attack);
}

// Valid inputs are 0-7, 0 = -12dB, ... 7 = +30dB
bool wm8960_setAlcMaxGain(uint8_t maxGain) 
{
	if(maxGain > 7) maxGain = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ALC1,6,4,maxGain);
}

// Valid inputs are 0-7, 0 = -17.25dB, ... 7 = +24.75dB
bool wm8960_setAlcMinGain(uint8_t minGain) 
{
	if(minGain > 7) minGain = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ALC2,6,4,minGain);
}

// Valid inputs are 0-15, 0 = 0ms, ... 15 = 43.691s
bool wm8960_setAlcHold(uint8_t hold) 
{
	if(hold > 15) hold = 15; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_ALC2,3,0,hold);
}

// Peak Limiter
bool wm8960_enablePeakLimiter(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ALC3, 8, 1);
}

bool wm8960_disablePeakLimiter(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ALC3, 8, 0);
}

// Noise Gate
bool wm8960_enableNoiseGate(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_NOISE_GATE, 0, 1);
}

bool wm8960_disableNoiseGate(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_NOISE_GATE, 0, 0);
}

// 0-31, 0 = -76.5dBfs, 31 = -30dBfs
bool wm8960_setNoiseGateThreshold(uint8_t threshold) 
{
	return true;
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// DAC
/////////////////////////////////////////////////////////

// Enable/disble each channel
bool wm8960_enableDacLeft(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 8, 1);
}

bool wm8960_disableDacLeft(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 8, 0);
}

bool wm8960_enableDacRight(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 7, 1);
}

bool wm8960_disableDacRight(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 7, 0);
}

// DAC digital volume
// Valid inputs are 0-255
// 0 = mute
// 1 = -127dB
// ... 0.5dB steps up to
// 255 = 0dB
bool wm8960_setDacLeftDigitalVolume(uint8_t volume)
{
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_LEFT_DAC_VOLUME,7,0,volume);
	bool result2 = wm8960_dacLeftDACVUSet();
	return (result1 && result2);
}

bool wm8960_setDacRightDigitalVolume(uint8_t volume)
{
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_RIGHT_DAC_VOLUME,7,0,volume);
	bool result2 = wm8960_dacRightDACVUSet();
	return (result1 && result2);
}

// Causes left and right input dac digital volumes to be updated
bool wm8960_dacLeftDACVUSet(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_DAC_VOLUME, 8, 1);
}

 // Causes left and right input dac digital volumes to be updated
bool wm8960_dacRightDACVUSet(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_DAC_VOLUME, 8, 1);
}	

// DAC digital volume DB
// Sets the volume of the DAC to a specified dB value passed in as a float 
// argument.
// Valid dB settings are -127.00 up to 0.0 (0.5dB steps)
// -127.50 (or lower) = MUTE
// -127.00 = -127.00dB (MIN)
// ... 0.5dB steps ...
// 0.00 = +0.00dB	(MAX)

bool wm8960_setDacLeftDigitalVolumeDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setDacLeftDigitalVolume(void)
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_DAC_GAIN_OFFSET, WM8960_DAC_GAIN_STEPSIZE, WM8960_DAC_GAIN_MIN, WM8960_DAC_GAIN_MAX);

	return wm8960_setDacLeftDigitalVolume(volume);
}

bool wm8960_setDacRightDigitalVolumeDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setDacRightDigitalVolume(void)
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_DAC_GAIN_OFFSET, WM8960_DAC_GAIN_STEPSIZE, WM8960_DAC_GAIN_MIN, WM8960_DAC_GAIN_MAX);

	return wm8960_setDacRightDigitalVolume(volume);
}

// DAC mute
bool wm8960_enableDacMute(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADC_DAC_CTRL_1, 3, 1);
}

bool wm8960_disableDacMute(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADC_DAC_CTRL_1, 3, 0);
}

// 3D Stereo Enhancement
// 3D enable/disable
bool wm8960_enable3d(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_3D_CONTROL, 0, 1);
}

bool wm8960_disable3d(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_3D_CONTROL, 0, 0);
}

bool wm8960_set3dDepth(uint8_t depth) // 0 = 0%, 15 = 100%
{
	if(depth > 15) depth = 15; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_3D_CONTROL,4,1,depth);
}

// DAC output -6dB attentuation enable/disable
bool wm8960_enableDac6dbAttenuation(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADC_DAC_CTRL_1, 7, 1);
}

bool wm8960_disableDac6dbAttentuation(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADC_DAC_CTRL_1, 7, 0);
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// OUTPUT mixers
/////////////////////////////////////////////////////////

// What's connected to what? Oh so many options...
// LOMIX	Left Output Mixer
// ROMIX	Right Output Mixer
// OUT3MIX		Mono Output Mixer

// Enable/disable left and right output mixers
bool wm8960_enableLOMIX(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 3, 1);
}

bool wm8960_disableLOMIX(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 3, 0);
}

bool wm8960_enableROMIX(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 2, 1);
}

bool wm8960_disableROMIX(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_3, 2, 0);
}

bool wm8960_enableOUT3MIX(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 1, 1);
}

bool wm8960_disableOUT3MIX(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 1, 0);
}

// Enable/disable audio path connections/vols to/from output mixers
// See datasheet page 35 for a nice image of all the connections.
bool wm8960_enableLI2LO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_OUT_MIX_1, 7, 1);
}

bool wm8960_disableLI2LO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_OUT_MIX_1, 7, 0);
}

// Valid inputs are 0-7. 0 = 0dB ...3dB steps... 7 = -21dB
bool wm8960_setLI2LOVOL(uint8_t volume) 
{
	if(volume > 7) volume = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_LEFT_OUT_MIX_1,6,4,volume);
}

bool wm8960_enableLB2LO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_BYPASS_1, 7, 1);
}

bool wm8960_disableLB2LO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_BYPASS_1, 7, 0);
}

// Valid inputs are 0-7. 0 = 0dB ...3dB steps... 7 = -21dB
bool wm8960_setLB2LOVOL(uint8_t volume) 
{
	if(volume > 7) volume = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_BYPASS_1,6,4,volume);
}

bool wm8960_enableLD2LO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_OUT_MIX_1, 8, 1);
}

bool wm8960_disableLD2LO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_LEFT_OUT_MIX_1, 8, 0);
}

bool wm8960_enableRI2RO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_OUT_MIX_2, 7, 1);
}

bool wm8960_disableRI2RO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_OUT_MIX_2, 7, 0);
}

// Valid inputs are 0-7. 0 = 0dB ...3dB steps... 7 = -21dB
bool wm8960_setRI2ROVOL(uint8_t volume) 
{
	if(volume > 7) volume = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_RIGHT_OUT_MIX_2,6,4,volume);
}

bool wm8960_enableRB2RO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_BYPASS_2, 7, 1);
}

bool wm8960_disableRB2RO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_BYPASS_2, 7, 0);
}

// Valid inputs are 0-7. 0 = 0dB ...3dB steps... 7 = -21dB
bool wm8960_setRB2ROVOL(uint8_t volume) 
{
	if(volume > 7) volume = 7; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_BYPASS_2,6,4,volume);
}

bool wm8960_enableRD2RO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_OUT_MIX_2, 8, 1);
}

bool wm8960_disableRD2RO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_RIGHT_OUT_MIX_2, 8, 0);
}

// Mono Output mixer. 
// Note, for capless HPs, we'll want this to output a buffered VMID.
// To do this, we need to disable both of these connections.
bool wm8960_enableLI2MO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_MONO_OUT_MIX_1, 7, 1);
}

bool wm8960_disableLI2MO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_MONO_OUT_MIX_1, 7, 0);
}

bool wm8960_enableRI2MO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_MONO_OUT_MIX_2, 7, 1);
}

bool wm8960_disableRI2MO(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_MONO_OUT_MIX_2, 7, 0);
}

// Enables VMID in the WM8960_REG_PWR_MGMT_2 register, and set's it to 
// playback/record settings of 2*50Kohm.
// Note, this function is only hear for backwards compatibility with the
// original releases of this library. It is recommended to use the
// setVMID(void) function instead.
bool wm8960_enableVMID(void)
{
	return wm8960_setVMID(WM8960_VMIDSEL_2X50KOHM);
}

bool wm8960_disableVMID(void)
{
	return wm8960_setVMID(WM8960_VMIDSEL_DISABLED);
}

// setVMID
// Sets the VMID signal to one of three possible settings.
// 4 options:
// WM8960_VMIDSEL_DISABLED
// WM8960_VMIDSEL_2X50KOHM (playback / record)
// WM8960_VMIDSEL_2X250KOHM (for low power / standby)
// WM8960_VMIDSEL_2X5KOHM (for fast start-up)
bool wm8960_setVMID(uint8_t setting)
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_PWR_MGMT_1, 8, 7, setting);
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// Headphones
/////////////////////////////////////////////////////////

// Enable and disable headphones (mute)
bool wm8960_enableHeadphones(void)
{
	return (wm8960_enableRightHeadphone() & wm8960_enableLeftHeadphone());
}

bool wm8960_disableHeadphones(void)
{
	return (wm8960_disableRightHeadphone() & wm8960_disableLeftHeadphone());
}

bool wm8960_enableRightHeadphone(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 5, 1);
}

bool wm8960_disableRightHeadphone(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 5, 0);
}

bool wm8960_enableLeftHeadphone(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 6, 1);
}

bool wm8960_disableLeftHeadphone(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 6, 0);
}

bool wm8960_enableHeadphoneStandby(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ANTI_POP_1, 0, 1);
}

bool wm8960_disableHeadphoneStandby(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_ANTI_POP_1, 0, 0);
}

// SetHeadphoneVolume
// Sets the volume for both left and right headphone outputs
// 
// Although you can control each headphone output independently, here we are
// Going to assume you want both left and right to do the same thing.
// 
// Valid inputs: 47-127. 0-47 = mute, 48 = -73dB ... 1dB steps ... 127 = +6dB
bool wm8960_setHeadphoneVolume(uint8_t volume) 
{		
	// Updates both left and right channels
	// Handles the OUT1VU (volume update) bit control, so that it happens at the 
	// same time on both channels. Note, we must also make sure that the outputs 
	// are enabled in the WM8960_REG_PWR_MGMT_2 [6:5]
	// Grab local copy of register
	// Modify the bits we need to
	// Write register in device, including the volume update bit write
	// If successful, save locally.

	// Limit inputs
	if (volume > 127) volume = 127;

	// LEFT
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_LOUT1_VOLUME,6,0,volume);
	// RIGHT
	bool result2 = wm8960_writeRegisterMultiBits(WM8960_REG_ROUT1_VOLUME,6,0,volume);
	// UPDATES

	// Updated left channel
	bool result3 = wm8960_writeRegisterBit(WM8960_REG_LOUT1_VOLUME, 8, 1); 

	// Updated right channel
	bool result4 = wm8960_writeRegisterBit(WM8960_REG_ROUT1_VOLUME, 8, 1); 

	if (result1 && result2 && result3 && result4) // If all writes ACK'd
	{
		return true;
	}
	return false; 
}

bool wm8960_enableHeadphoneJackDetect(void) {
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_2, 6, 1);
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_1, 0, 1); // TOEN - slow clock enable
	return result1 && result2;
}

bool wm8960_disableHeadphoneJackDetect(void) {
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_2, 6, 0);
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_1, 0, 0); // TOEN - slow clock disable
	return result1 && result2;
}

// SetHeadphoneJackDetectInput
// Sets the input pin for jack insertion detection
//
// Valid inputs: WM8960_JACKDETECT_LINPUT3, WM8960_JACKDETECT_RINPUT3, WM8960_JACKDETECT_GPIO1
bool wm8960_setHeadphoneJackDetectInput(uint8_t setting) 
{
	bool result1 = false;
	bool result2 = false;
	switch (setting) {
	case WM8960_JACKDETECT_LINPUT3:
		result1 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 3, 1);
		result2 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 2, 0); // JD2
		break;
	case WM8960_JACKDETECT_RINPUT3:
		result1 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 3, 1);
		result2 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 2, 1); // JD3
		break;
	default:
		result1 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 3, 0); // GPIO1
		result2 = wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_4, 2, 0); // defaults to 0
		break;
	}
	return result1 && result2;
}

// Set headphone volume dB
// Sets the volume of the headphone output buffer amp to a specified dB value 
// passed in as a float argument.
// Valid dB settings are -74.0 up to +6.0
// Note, we are accepting float arguments here, in order to keep it consistent
// with other volume setting functions in this library that can do partial dB
// values (such as the PGA, ADC and DAC gains).
// -74 (or lower) = MUTE
// -73 = -73dB (MIN)
// ... 1dB steps ...
// 0 = 0dB
// ... 1dB steps ...
// 6 = +6dB	(MAX)
bool wm8960_setHeadphoneVolumeDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setHeadphoneVolume(void)
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_HP_GAIN_OFFSET, WM8960_HP_GAIN_STEPSIZE, WM8960_HP_GAIN_MIN, WM8960_HP_GAIN_MAX);

	return wm8960_setHeadphoneVolume(volume);
}

// Zero Cross prevents zipper sounds on volume changes
// Sets both left and right Headphone outputs
bool wm8960_enableHeadphoneZeroCross(void)
{
	// Left
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_LOUT1_VOLUME, 7, 1); 

	// Right
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ROUT1_VOLUME, 7, 1); 
	return (result1 & result2);
}

bool wm8960_disableHeadphoneZeroCross(void)
{
	// Left
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_LOUT1_VOLUME, 7, 0); 

	// Right
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ROUT1_VOLUME, 7, 0); 
	return (result1 & result2);
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// Speakers
/////////////////////////////////////////////////////////

// Enable and disable speakers (mute)
bool wm8960_enableSpeakers(void)
{
	return (wm8960_enableRightSpeaker() & wm8960_enableLeftSpeaker());
}

bool wm8960_disableSpeakers(void)
{
	return (wm8960_disableRightHeadphone() & wm8960_disableLeftHeadphone());
}

bool wm8960_enableRightSpeaker(void)
{
	// SPK_OP_EN
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_CLASS_D_CONTROL_1, 7, 1); 

	// SPKR
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 3, 1); 
	return (result1 & result2);
}

bool wm8960_disableRightSpeaker(void)
{
	// SPK_OP_EN
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_CLASS_D_CONTROL_1, 7, 0); 

	// SPKR
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 3, 0); 
	return (result1 & result2);
}

bool wm8960_enableLeftSpeaker(void)
{
	// SPK_OP_EN
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_CLASS_D_CONTROL_1, 6, 1); 

	// SPKL
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 4, 1); 
	return (result1 & result2);
}

bool wm8960_disableLeftSpeaker(void)
{
	// SPK_OP_EN
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_CLASS_D_CONTROL_1, 6, 0); 

	// SPKL
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 4, 0); 
	return (result1 & result2);
}

// SetSpeakerVolume(SPKLVOL and SPKRVOL)
// Sets to volume for both left and right speaker outputs
// 
// Although you can control each Speaker output independently, here we are
// Going to assume you want both left and right to do the same thing.
// 
// Valid inputs are 47-127. 0-47 = mute, 48 = -73dB, ... 1dB steps ... , 127 = +6dB
bool wm8960_setSpeakerVolume(uint8_t volume) 
{		
	// Updates both left and right channels
	// Handles the SPKVU (volume update) bit control, so that it happens at the 
	// same time on both channels. Note, we must also make sure that the outputs 
	// are enabled in the WM8960_REG_PWR_MGMT_2 [4:3], and the class D control 
	// reg WM8960_REG_CLASS_D_CONTROL_1 [7:6]

	// Limit inputs
	if (volume > 127) volume = 127;

	// LEFT
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_LOUT2_VOLUME,6,0,volume);

	// RIGHT
	bool result2 = wm8960_writeRegisterMultiBits(WM8960_REG_ROUT2_VOLUME,6,0,volume);

	// SPKVU

	// Updated left channel
	bool result3 = wm8960_writeRegisterBit(WM8960_REG_LOUT2_VOLUME, 8, 1); 

	// Updated right channel
	bool result4 = wm8960_writeRegisterBit(WM8960_REG_ROUT2_VOLUME, 8, 1); 

	if (result1 && result2 && result3 && result4) // If all writes ACK'd
	{
		int vol_db = (int)volume - 48 - 73; // Convert to dB for logging
		if(vol_db < -73) {
			ESP_LOGI(TAG, "Speaker volume set to MUTE (%d)", vol_db);
		}
		else {
			ESP_LOGI(TAG, "Speaker volume set to %d(%d dB)", volume, vol_db);
		}
		
		return true;
	}
	return false;
}

uint8_t wm8960_getSpeakerVolume(void)
{
	return (uint8_t)wm8960_readRegisterMultiBits(WM8960_REG_LOUT2_VOLUME,6,0);
}

// Set speaker volume dB(SPKLVOL and SPKRVOL)
// Sets the volume of the class-d speaker output amp to a specified dB value 
// passed in as a float argument.
// Valid dB settings are -74.0 up to +6.0
// Note, we are accepting float arguments here, in order to keep it consistent
// with other volume setting functions in this library that can do partial dB
// values (such as the PGA, ADC and DAC gains).
// -74 (or lower) = MUTE
// -73 = -73dB (MIN)
// ... 1dB steps ...
// 0 = 0dB
// ... 1dB steps ...
// 6 = +6dB	(MAX)
bool wm8960_setSpeakerVolumeDB(float dB)
{
	// Create an unsigned integer volume setting variable we can send to 
	// setSpeakerVolume(void)
	uint8_t volume = wm8960_convertDBtoSetting(dB, WM8960_SPEAKER_GAIN_OFFSET, WM8960_SPEAKER_GAIN_STEPSIZE, WM8960_SPEAKER_GAIN_MIN, WM8960_SPEAKER_GAIN_MAX);

	return wm8960_setSpeakerVolume(volume);
}

// Zero Cross prevents zipper sounds on volume changes
// Sets both left and right Speaker outputs
bool wm8960_enableSpeakerZeroCross(void)
{
	// Left
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_LOUT2_VOLUME, 7, 1); 

	// Right
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ROUT2_VOLUME, 7, 1); 
	return (result1 & result2);
}

bool wm8960_disableSpeakerZeroCross(void)
{
	// Left
	bool result1 = wm8960_writeRegisterBit(WM8960_REG_LOUT2_VOLUME, 7, 0); 

	// Right
	bool result2 = wm8960_writeRegisterBit(WM8960_REG_ROUT2_VOLUME, 7, 0); 
	return (result1 & result2);
}

// SetSpeakerDcGain
// DC and AC gain - allows signal to be higher than the DACs swing
// (use only if your SPKVDD is high enough to handle a larger signal)
// Valid inputs are 0-5
// 0 = +0dB (1.0x boost) ... up to ... 5 = +5.1dB (1.8x boost)
bool wm8960_setSpeakerDcGain(uint8_t gain)
{
	if(gain > 5) gain = 5; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLASS_D_CONTROL_3,5,3,gain);
}

// SetSpeakerAcGain
// DC and AC gain - allows signal to be higher than the DACs swing
// (use only if your SPKVDD is high enough to handle a larger signal)
// Valid inputs are 0-5
// 0 = +0dB (1.0x boost) ... up to ... 5 = +5.1dB (1.8x boost)
bool wm8960_setSpeakerAcGain(uint8_t gain)
{
	if(gain > 5) gain = 5; // Limit incoming values max
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLASS_D_CONTROL_3,2,0,gain);
}

//////////////////////////////////////////////
////////////////////////////////////////////// Digital audio interface control
//////////////////////////////////////////////

// Defaults to I2S, peripheral-mode, 24-bit word length

// Loopback
// When enabled, the output data from the ADC audio interface is fed directly 
// into the DAC data input.
bool wm8960_enableLoopBack(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_2, 0, 1);
}

bool wm8960_disableLoopBack(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_2, 0, 0);
}

/////////////////////////////////////////////////////////
///////////////////////////////////////////////////////// Clock controls
/////////////////////////////////////////////////////////

// Getting the Frequency of SampleRate as we wish
// Our MCLK (an external clock on the SFE breakout board) is 24.0MHz.
// According to table 40 (DS pg 58), we want SYSCLK to be 11.2896 for a SR of 
// 44.1KHz. To get that Desired Output (SYSCLK), we need the following settings 
// on the PLL stuff, as found on table 45 (ds pg 61):
// PRESCALE DIVIDE (PLLPRESCALE): 2
// POSTSCALE DVIDE (SYSCLKDIV[1:0]): 2
// FIXED POST-DIVIDE: 4
// R: 7.5264 
// N: 7h
// K: 86C226h

// Example at bottom of table 46, shows that we should be in fractional mode 
// for a 44.1KHz.

// In terms of registers, this is what we want for 44.1KHz
// PLLEN=1			(PLL enable)
// PLLPRESCALE=1	(divide by 2) *This get's us from MCLK (24MHz) down to 12MHZ 
// for F2.
// PLLN=7h			(PLL N value) *this is "int R"
// PLLK=86C226h		(PLL K value) *this is int ( 2^24 * (R- intR)) 
// SDM=1			(Fractional mode)
// CLKSEL=1			(PLL select) 
// MS=0				(Peripheral mode)
// WL=00			(16 bits)
// SYSCLKDIV=2		(Divide by 2)
// ADCDIV=000		(Divide by 1) = 44.1kHz
// DACDIV=000		(Divide by 1) = 44.1kHz
// BCLKDIV=0100		(Divide by 4) = 64fs
// DCLKDIV=111		(Divide by 16) = 705.6kHz

// And now for the functions that will set these registers...
bool wm8960_enablePLL(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 0, 1);
}

bool wm8960_disablePLL(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_PWR_MGMT_2, 0, 0);
}

bool wm8960_setPLLPRESCALE(bool div)
{
	return wm8960_writeRegisterBit(WM8960_REG_PLL_N, 4, div);
}

bool wm8960_setPLLN(uint8_t n)
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_PLL_N,3,0,n); 
}

// Send each nibble of 24-bit value for value K
bool wm8960_setPLLK(uint8_t one, uint8_t two, uint8_t three) 
{
	bool result1 = wm8960_writeRegisterMultiBits(WM8960_REG_PLL_K_1,5,0,one); 
	bool result2 = wm8960_writeRegisterMultiBits(WM8960_REG_PLL_K_2,8,0,two); 
	bool result3 = wm8960_writeRegisterMultiBits(WM8960_REG_PLL_K_3,8,0,three); 
	if (result1 && result2 && result3) // If all I2C sommands Ack'd, then...
	{
	return true;
	}
	return false;	
}

// 0=integer, 1=fractional
bool wm8960_setSMD(bool mode)
{
	return wm8960_writeRegisterBit(WM8960_REG_PLL_N, 5, mode);
}

 // 0=MCLK, 1=PLL_output
bool wm8960_setCLKSEL(bool sel)
{
	return wm8960_writeRegisterBit(WM8960_REG_CLOCKING_1, 0, sel);
}

// (0=divide by 1), (2=div by 2) *1 and 3 are "reserved"
bool wm8960_setSYSCLKDIV(uint8_t div) 
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLOCKING_1,2,1,div);	
}

// 000 = SYSCLK / (1.0*256). See ds pg 57 for other options
bool wm8960_setADCDIV(uint8_t div) 
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLOCKING_1,8,6,div);	
}

// 000 = SYSCLK / (1.0*256). See ds pg 57 for other options
bool wm8960_setDACDIV(uint8_t div) 
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLOCKING_1,5,3,div);	
}

bool wm8960_setBCLKDIV(uint8_t div)
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLOCKING_2,3,0,div);	
}

// Class D amp, 111= SYSCLK/16, so 11.2896MHz/16 = 705.6KHz
bool wm8960_setDCLKDIV(uint8_t div) 
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_CLOCKING_2,8,6,div);
}

bool wm8960_setALRCGPIO(void)
{
	// This setting should not be changed if ADCs are enabled.
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_2, 6, 1);
}

bool wm8960_enableMasterMode(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_1, 6, 1);
}

bool wm8960_enablePeripheralMode(void)
{
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_1, 6, 0);
}

bool wm8960_setWL(uint8_t word_length)
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_AUDIO_INTERFACE_1,3,2,word_length);	
}

bool wm8960_setLRP(bool polarity)
{
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_1, 4, polarity);
}

bool wm8960_setALRSWAP(bool swap)
{
	return wm8960_writeRegisterBit(WM8960_REG_AUDIO_INTERFACE_1, 8, swap);
}

bool wm8960_setFORMAT(uint8_t format)
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_AUDIO_INTERFACE_1, 1,0, format);
}

bool wm8960_setVROI(bool setting)
{
	return wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_3, 6, setting);
}

bool wm8960_setVSEL(uint8_t setting)
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_ADDITIONAL_CONTROL_1,7,6,setting); 
}

bool wm8960_dac_stereomono(bool stereo)	//0 stereo, 1 mono
{
	return wm8960_writeRegisterBit(WM8960_REG_ADDITIONAL_CONTROL_1, 4, stereo);
}

bool wm8960_adc_datasel(uint8_t sel)	//0 l2l r2r, 1 l2l l2r, 2 r2l r2r, 3 r2l l2r
{
	return wm8960_writeRegisterMultiBits(WM8960_REG_ADDITIONAL_CONTROL_1, 3,2, sel);
}

// convertDBtoSetting
// This function will take in a dB value (as a float), and return the 
// corresponding volume setting necessary.
// For example, Headphone volume control goes from 47-120.
// While PGA gain control is from 0-63.
// The offset values allow for proper conversion.
//
// dB - float value of dB
//
// offset - the differnce from lowest dB value to lowest setting value
//
// stepSize - the dB step for each setting (aka the "resolution" of the setting)
// This is 0.75dB for the PGAs, 0.5 for ADC/DAC, and 1dB for most other amps.
//
// minDB - float of minimum dB setting allowed, note this is not mute on the 
// amp. "True mute" is always one stepSize lower.
//
// maxDB - float of maximum dB setting allowed. If you send anything higher, it
// will be limited to this max value.
uint8_t wm8960_convertDBtoSetting(float dB, float offset, float stepSize, float minDB, float maxDB)
{
	// Limit incoming dB values to acceptable range. Note, the minimum limit we
	// want to limit this too is actually one step lower than the minDB, because
	// that is still an acceptable dB level (it is actually "true mute").
	// Note, the PGA amp does not have a "true mute" setting available, so we 
	// must check for its unique minDB of -17.25.

	// Limit max. This is the same for all amps.
	if (dB > maxDB) dB = maxDB;

	// PGA amp doesn't have mute setting, so minDB should be limited to minDB
	// Let's check for the PGAs unique minDB (-17.25) to know we are currently
	// converting a PGA setting.
	if(minDB == WM8960_PGA_GAIN_MIN) 
	{
		if (dB < minDB) dB = minDB;
	}
	else // Not PGA. All other amps have a mute setting below minDb
	{
		if (dB < (minDB - stepSize)) dB = (minDB - stepSize);
	}

	// Adjust for offset
	// Offset is the number that gets us from the minimum dB option of an amp
	// up to the minimum setting value in the register.
	dB = dB + offset; 

	// Find out how many steps we are above the minimum (at this point, our 
	// minimum is "0". Note, because dB comes in as a float, the result of this 
	// division (volume) can be a partial number. We will round that next.
	float volume = dB / stepSize;

	volume = round(volume); // round to the nearest setting value.

	// Serial debug (optional)
	// Serial.print("\t");
	// Serial.print((uint8_t)volume);

	return (uint8_t)volume; // cast from float to unsigned 8-bit integer.
}

static void wm8960_i2c_init(void)
{
	esp_err_t rc;
	i2c_config_t i2c_conf = {
		//memset(&i2c_conf, 0x00, sizeof(i2c_conf));
		.mode = I2C_MODE_MASTER,
		.sda_io_num = SDA1,
		.scl_io_num = SCL1,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 100 * 1000,
	};

	ESP_LOGI(TAG, "Configuring I2C");
	rc = i2c_param_config(I2C_BUS_NO, &i2c_conf);
	ESP_LOGI(TAG, "I2C Param Config: %s", esp_err_to_name(rc));
	rc = i2c_driver_install(I2C_BUS_NO, I2C_MODE_MASTER, 0, 0, 0);
	ESP_LOGI(TAG, "I2C Driver Install; %s", esp_err_to_name(rc));
}

esp_err_t wm8960_deinit(void)
{
	esp_err_t rc;
	wm8960_reset();
	rc = i2c_driver_delete(I2C_BUS_NO);
	ESP_LOGI(TAG, "I2C Driver Delete: %s", esp_err_to_name(rc));
	return rc;
}

esp_err_t wm8960_init(void)
{
	esp_err_t ret = 0;

	esp_log_level_set(TAG, ESP_LOG_INFO);

	wm8960_i2c_init();

	if(wm8960_isConnected()) {
		ESP_LOGI(TAG, "WM8960 is connected");
	} else {
		ESP_LOGE(TAG, "WM8960 not found");
		while(1) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			if(wm8960_isConnected()) {
				ESP_LOGI(TAG, "WM8960 is connected");
				break;
			}
			else {
				ESP_LOGE(TAG, "WM8960 not found");
			}
		}
	}

	wm8960_reset();
	vTaskDelay(pdMS_TO_TICKS(10));

	/****** COMMON ******/
	// General setup needed
	wm8960_enableVREF();
	wm8960_enableVMID();
	wm8960_setFORMAT(WM8960_FORMAT_I2S); // 
	wm8960_setWL(WM8960_WL_16BIT);
	wm8960_adc_datasel(WM8960_ADC_LR_NORMAL);
	wm8960_dac_stereomono(WM8960_DAC_MONO); // false = stereo, true = mono

#ifdef SYSCLK_FROM_PLL
	wm8960_setCLKSEL(WM8960_CLKSEL_PLL);
	// CLOCK STUFF, These settings will get you 44.1KHz sample rate, and class-d 
	// freq at 705.6kHz
	wm8960_enablePLL(); // Needed for class-d amp clock
	wm8960_setPLLPRESCALE(WM8960_PLLPRESCALE_DIV_2);
	wm8960_setSMD(WM8960_PLL_MODE_FRACTIONAL);
	wm8960_setSYSCLKDIV(WM8960_SYSCLK_DIV_BY_2);
	wm8960_setDCLKDIV(WM8960_DCLKDIV_16);
	wm8960_setPLLN(7);
	wm8960_setPLLK(0x86, 0xC2, 0x26); // PLLK=86C226h	
	wm8960_setADCDIV(3); // Default is 000
	wm8960_setDACDIV(3); // Default is 000
#else
	wm8960_setCLKSEL(WM8960_CLKSEL_MCLK);
	wm8960_disablePLL(); // Needed for class-d amp clock
	wm8960_setSYSCLKDIV(WM8960_SYSCLK_DIV_BY_1);

	wm8960_setDCLKDIV(WM8960_DCLKDIV_16);	//12.288MHz/16 = 768kHz
	wm8960_setADCDIV(WM8960_ADAC_DIV_768);	//12.288MHz/768 = 16kHz
	wm8960_setDACDIV(WM8960_ADAC_DIV_768);	//12.288MHz/768 = 16kHz
#endif


	/****** MIC ******/
	// Setup signal flow through the analog audio bypass connections
	wm8960_enableLMIC();
	wm8960_enableRMIC();

	// Connect from INPUT1 to "n" (aka inverting) inputs of PGAs.
	wm8960_connectLMN1();
	wm8960_connectRMN1();
	wm8960_pgaLeftNonInvSignalSelect(WM8960_PGAL_VMID);
	wm8960_pgaRightNonInvSignalSelect(WM8960_PGAR_VMID);

	// Set input boosts(0, 13, 20, 29dB) to get INPUT1 to the boost mixers
	wm8960_setLMICBOOST(WM8960_MIC_BOOST_GAIN_0DB);
	wm8960_setRMICBOOST(WM8960_MIC_BOOST_GAIN_0DB);
//	wm8960_setLMICBOOST(WM8960_MIC_BOOST_GAIN_20DB);
//	wm8960_setRMICBOOST(WM8960_MIC_BOOST_GAIN_20DB);

	// Connect from MICs to boost mixers
	wm8960_connectLMIC2B();
	wm8960_connectRMIC2B();

	// Enable Boot mixers
	wm8960_enableAINL();
	wm8960_enableAINR();

	// Disable mutes on PGA inputs (aka INTPUT1)
	wm8960_disableLINMUTE();
	wm8960_disableRINMUTE();
	
	/****** ADC ******/
	// Enable ADCs
	wm8960_enableAdcLeft();
	wm8960_enableAdcRight();

	wm8960_setAdcLeftDigitalVolume(195);	// Valid inputs are 0-255, 0 = mute, 1 = -97dB, 0.5dB steps, 195 = +0dB, 255 = +30dB
	wm8960_setAdcRightDigitalVolume(195); // Valid inputs are 0-255, 0 = mute, 1 = -97dB, 0.5dB steps, 195 = +0dB, 255 = +30dB
//	wm8960_setAdcLeftDigitalVolume(255);	// Valid inputs are 0-255, 0 = mute, 1 = -97dB, 0.5dB steps, 195 = +0dB, 255 = +30dB
//	wm8960_setAdcRightDigitalVolume(255); // Valid inputs are 0-255, 0 = mute, 1 = -97dB, 0.5dB steps, 195 = +0dB, 255 = +30dB

	// Set pga volumes (-17.25dB to +30.00dB)
//	wm8960_setLINVOLDB(12.00); // Valid options are -17.25dB to +30.00dB
//	wm8960_setRINVOLDB(12.00); // Valid options are -17.25dB to +30.00dB
	wm8960_setLINVOLDB(30.00); // Valid options are -17.25dB to +30.00dB
	wm8960_setRINVOLDB(30.00); // Valid options are -17.25dB to +30.00dB

	/****** DAC ******/
	// Enable DACs
	wm8960_enableDacLeft();
	wm8960_enableDacRight();
	// Connect from DAC outputs to output mixer
	wm8960_enableLD2LO();
	wm8960_enableRD2RO();

	wm8960_setDacLeftDigitalVolume(255);	// Valid options are 0(mute), 1(-127dB) to 255(0dB), 0.5dB steps
	wm8960_setDacRightDigitalVolume(255); // Valid options are 0(mute), 1(-127dB) to 255(0dB), 0.5dB steps

	wm8960_disableDacMute();

	/****** SPEAKER OUTPUT ******/
	wm8960_enableLOMIX();
	wm8960_enableROMIX();
	wm8960_enableSpeakers();
	wm8960_setSpeakerVolume(speaker_volume); // Valid inputs are 47-127. 0-47 = mute, 48 = -73dB, ... 1dB steps ... , 127 = +6dB

	return ret;
}

esp_err_t wm8960_adc_onoff(bool on)
{
	if(on) {
		wm8960_enableLMIC();
		wm8960_enableRMIC();
		wm8960_enableAINL();
		wm8960_enableAINR();
		wm8960_enableAdcLeft();
		wm8960_enableAdcRight();
	} else {
		wm8960_disableAdcLeft();
		wm8960_disableAdcRight();
		wm8960_disableAINL();
		wm8960_disableAINR();
		wm8960_disableLMIC();
		wm8960_disableRMIC();
	}
	return 0;
}

esp_err_t wm8960_dac_onoff(bool on)
{
	if(on) {
		wm8960_enableDacLeft();
		wm8960_enableDacRight();
		wm8960_enableLD2LO();
		wm8960_enableRD2RO();
		wm8960_disableDacMute();
	} else {
		wm8960_enableDacMute();
		wm8960_disableRD2RO();
		wm8960_disableLD2LO();
		wm8960_disableDacLeft();
		wm8960_disableDacRight();
	}
	return 0;
}

esp_err_t wm8960_both_onoff(bool on)
{
	if(on) {
		wm8960_adc_onoff(true);
		wm8960_dac_onoff(true);
	} else {
		wm8960_dac_onoff(false);
		wm8960_adc_onoff(false);
	}
	return 0;
}

esp_err_t wm8960_speaker_onoff(bool on)
{
	ESP_LOGI(TAG, "%s, %s", __func__, on ? "ON" : "OFF");
	if(on) {
		wm8960_enableLOMIX();
		wm8960_enableROMIX();
		wm8960_enableSpeakers();
		wm8960_setSpeakerVolume(speaker_volume); // Valid inputs are 47-127. 0-47 = mute, 48 = -73dB, ... 1dB steps ... , 127 = +6dB
	} else {
		wm8960_setSpeakerVolume(0); // Mute
		wm8960_disableSpeakers();
		wm8960_disableROMIX();
		wm8960_disableLOMIX();
	}
	return 0;
}
