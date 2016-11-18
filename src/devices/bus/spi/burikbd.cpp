#include "burikbd.h"

const device_type SPI_KEYBOARD = &device_creator<spi_kbd_device>;

spi_kbd_device::spi_kbd_device(const machine_config &mconfig, const char *tag,
                               device_t *owner, uint32_t clock)
	: spi_slave_device(mconfig, tag, owner, clock),
	m_keyboard_dev(*this, "at_keyboard"),
	m_write_irq(*this), m_state(SPI_KBD_NOT_SELECTED),
	m_last_scancode(0), m_scancode_reg_full(false)
{ }

static MACHINE_CONFIG_FRAGMENT( keyboard )
	MCFG_AT_KEYB_ADD("at_keyboard", 1, WRITELINE(spi_kbd_device, keyboard_w))
MACHINE_CONFIG_END

machine_config_constructor spi_kbd_device::device_mconfig_additions() const
{
	return MACHINE_CONFIG_NAME( keyboard );
}

WRITE_LINE_MEMBER(spi_kbd_device::keyboard_w)
{
	// NOP if keyboard data ready line is 0
	if(!state) { return; }

	// Read keyboard scancode
	m_last_scancode = m_keyboard_dev->read(machine().dummy_space(), 0);

	// Indicate that we have data to read
	m_scancode_reg_full = true;
	m_write_irq(1);
}

void spi_kbd_device::device_start()
{
	spi_slave_device::device_start();
	m_write_irq.resolve_safe();
}

void spi_kbd_device::device_reset()
{
	spi_slave_device::device_reset();
	m_state = SPI_KBD_NOT_SELECTED;
	m_scancode_reg_full = false;
	m_last_scancode = 0;
	m_write_irq(0);
}

void spi_kbd_device::spi_slave_select()
{
	m_state = SPI_KBD_NEWLY_SELECTED;
}

void spi_kbd_device::spi_slave_deselect()
{
	m_state = SPI_KBD_NOT_SELECTED;
}

void spi_kbd_device::spi_slave_mosi_byte(uint8_t recv_byte)
{
	switch(m_state) {
	case SPI_KBD_NEWLY_SELECTED:
		if(recv_byte & 0x80) {
			// control
			m_state = SPI_KBD_READY_TO_RESPOND;
			set_miso_byte(control(recv_byte & 0x7F));
			return;
		} else {
			// read
			m_state = SPI_KBD_READY_TO_READ;
			set_miso_byte(m_last_scancode);

			// clear buffer
			m_last_scancode = 0x00;
			m_scancode_reg_full = false;
			m_write_irq(0);
		}
		break;
	case SPI_KBD_READY_TO_READ:
	case SPI_KBD_READY_TO_RESPOND:
		m_state = SPI_KBD_DONE;
		set_miso_byte(0x00);
		break;
	default:
		set_miso_byte(0x00);
		break;
	}
}

// Called when there is a new control byte. Returns the response from the
// control byte.
uint8_t spi_kbd_device::control(uint8_t ctrl_byte)
{
	switch(ctrl_byte) {
	case 0x00:
		device_reset();
		return 0x00;
	case 0x01:
		return m_scancode_reg_full ? 0x00 : 0xFF;
	default:
		return 0x00;
	}
}
