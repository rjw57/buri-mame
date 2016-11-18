// license:BSD-3-Clause
// copyright-holders:Rich Wreham
#include "slave.h"

const device_type SPI_SLAVE = &device_creator<spi_slave_device>;

spi_slave_device::spi_slave_device(const machine_config &mconfig, const char *tag,
                       device_t *owner, uint32_t clock )
	: device_t(mconfig, SPI_SLAVE, "SPI slave", tag, owner, clock, "spislave", __FILE__),
	m_mode(SPI_MODE0), m_data_dir(SPI_MSB_FIRST),
	m_selected(false),
	m_clk(0), m_mosi(0), m_miso(0),
	m_recv_byte(0), m_send_byte(0),
	m_recv_count(0), m_send_count(0),
	m_write_miso(*this)
{
}

void spi_slave_device::device_start()
{
	// resolve callbacks
	m_write_miso.resolve_safe();
}

void spi_slave_device::device_reset()
{
	m_recv_count = m_send_count = 0;
	m_recv_byte = m_send_byte = 0;
}

WRITE_LINE_MEMBER(spi_slave_device::write_mosi)
{
	m_mosi = (state != 0);
}

WRITE_LINE_MEMBER(spi_slave_device::write_select)
{
	if(state != m_selected) {
		if(state) {
			// newly selected, clear recv/send counts
			m_recv_count = m_send_count = 0;
			spi_slave_select();
		} else {
			spi_slave_deselect();
		}
	}
	m_selected = (state != 0);
}

WRITE_LINE_MEMBER(spi_slave_device::write_clock)
{
	// Ignore no-change
	if(m_clk == state) { return; }

	// Update state
	m_clk = state;

	// That's it if we're not selected
	if(!m_selected) { return; }

	if(state) {
		// low to high
		clk_edge_(cpol() ? 0 : 1);
	} else {
		// high to low
		clk_edge_(cpol() ? 1 : 0);
	}
}

READ_LINE_MEMBER(spi_slave_device::read_miso)
{
	return m_miso;
}

void spi_slave_device::clk_edge_(int is_idle_to_active)
{
	int data_stable = cpha() ? !is_idle_to_active : is_idle_to_active;

	if(data_stable) {
		// data is stable, read MOSI
		if(m_data_dir == SPI_MSB_FIRST) {
			m_recv_byte <<= 1;
			m_recv_byte |= m_mosi ? 0x01 : 0x00;
		} else {
			m_recv_byte >>= 1;
			m_recv_byte |= m_mosi ? 0x80 : 0x00;
		}
		++m_recv_count;
	} else {
		// data lines can be changed
		if(m_data_dir == SPI_MSB_FIRST) {
			set_miso((m_send_byte & 0x80) ? 1 : 0);
			m_send_byte <<= 1;
		} else {
			set_miso(m_send_byte & 0x1);
			m_send_byte >>= 1;
		}
		++m_send_count;
	}

	if((m_recv_count == 8) && (m_send_count == 8)) {
		// sent and received an entire byte
		set_miso_byte(0x00); // reset send byte
		spi_slave_mosi_byte(m_recv_byte);
	}
}

void spi_slave_device::set_miso_byte(uint8_t send_byte)
{
	m_send_byte = send_byte;

	// set MISO depending on data direction
	if(m_data_dir == SPI_MSB_FIRST) {
		set_miso((m_send_byte & 0x80) ? 1 : 0);
	} else {
		set_miso(m_send_byte & 0x1);
	}
}

void spi_slave_device::spi_slave_select() { }
void spi_slave_device::spi_slave_deselect() { }
void spi_slave_device::spi_slave_mosi_byte(ATTR_UNUSED uint8_t) { }
