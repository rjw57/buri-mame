// license:BSD-3-Clause
// copyright-holders:Rich Wareham
/*
 * Búri SPI keyboard peripheral
 */

#ifndef SPI_BURIKBD_H
#define SPI_BURIKBD_H

#include "emu.h"
#include "bus/spi/slave.h"
#include "machine/pckeybrd.h"

// SPI keyboard controller
// -----------------------
//
// The SPI keyboard controller talks to a hardware keyboard and presents an
// AT-scancode set 1 keyboard over SPI. There are two modes: read scancode and
// write control.
//
// After selecting the device, the master exchanges two bytes with the device.
// Subsequent SPI exchanges are ignored with MISO set to 0.
//
// Read scancode
// ~~~~~~~~~~~~~
//
// | MOSI | MISO     |
// |======|==========|
// | $00  | <X>      |
// | <X>  | scancode |
//
// After the scancode is read, the internal scancode register is reset to $00.
// Subsequent reads will therefore return $00.
//
// Write control
// ~~~~~~~~~~~~~
// Writing is indicated by sending a byte with the high bit set. The low 7 bits
// are the control code. The control response is sent in the next byte.
//
// | MOSI       | MISO     |
// |============|==========|
// | $80 |<ctrl | <X>      |
// | <X>        | response |
//
// Unknown control codes may have unintended effects and have undefined
// responses.
//
// Control codes
// ~~~~~~~~~~~~~
//
// $00 - reset the controller
// $01 - responds $FF if scancode register is full or $00 if empty

#define MCFG_SPI_KBD_ADD( _tag ) \
	MCFG_DEVICE_ADD( _tag, SPI_KEYBOARD, 0 ) \
	MCFG_SPI_MODE(SPI_MODE1) \
	MCFG_SPI_DATA_DIRECTION(SPI_MSB_FIRST)

#define MCFG_SPI_KBD_IRQ_CALLBACK(_irq) \
	downcast<spi_kbd_device *>(device)->set_irq_callback(DEVCB_##_irq);

enum spi_kbd_state {
	SPI_KBD_NOT_SELECTED,
	SPI_KBD_NEWLY_SELECTED,
	SPI_KBD_READY_TO_READ,
	SPI_KBD_READY_TO_RESPOND,
	SPI_KBD_DONE,
};

class spi_kbd_device : public spi_slave_device
{
public:
	spi_kbd_device(const machine_config &mconfig, const char *tag,
	               device_t *owner, uint32_t clock);

	template<class _irq> void set_irq_callback(_irq irq) {
		m_write_irq.set_callback(irq);
	}

	virtual machine_config_constructor device_mconfig_additions() const override;

	DECLARE_WRITE_LINE_MEMBER( keyboard_w );

protected:
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual void spi_slave_select() override;
	virtual void spi_slave_deselect() override;
	virtual void spi_slave_mosi_byte(uint8_t) override;

	uint8_t control(uint8_t ctrl_byte);

	required_device<at_keyboard_device> m_keyboard_dev;

	devcb_write_line m_write_irq;

	spi_kbd_state m_state;
	uint8_t m_last_scancode;
	bool m_scancode_reg_full;
};

extern const device_type SPI_KEYBOARD;

#endif
