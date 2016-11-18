// license:BSD-3-Clause
// copyright-holders:Rich Wareham
#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include "emu.h"

// SPI slave device
// ----------------
//
// A SPI slave device exposes three serial lines, MOSI, MISO and CLK which
// understand the SPI protocol. There is also a select line which is used to
// enable the device. Setting MOSI or CLK will have no effect when the select
// line is 0. Two parameters are used to define the exact flavour of SPI:
//
// 1. SPI "mode": 0-3 defines clock idle and active states along with when data
//    and commands are read.
// 2. Data direction: whether bytes are sent most significant bit (MSB) first or
//    least significant bit first (LSB).
//
// SPI is full duplex; each communication exchanges a byte from the master to
// the slave and a byte from the slave to the master. Use set_next_send_byte()
// to specify what the slave will send on the next exchange. This can be called
// from the data receive callback. This callback is called when a byte from the
// master has been received.

enum spi_mode_t { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };
enum spi_data_direction_t { SPI_MSB_FIRST, SPI_LSB_FIRST };

#define MCFG_SPI_ADD( _tag ) MCFG_DEVICE_ADD( _tag, SPI, 0 )
#define MCFG_SPI_MODE( mode ) spi_slave_device::set_mode(*device, mode);
#define MCFG_SPI_DATA_DIRECTION( dir ) spi_slave_device::set_data_direction(*device, dir);
#define MCFG_SPI_MISO_CALLBACK(_miso) \
	downcast<spi_slave_device *>(device)->set_miso_callback(DEVCB_##_miso);
#define MCFG_SPI_RECV_BYTE_CALLBACK(_recv_byte) \
	downcast<spi_slave_device *>(device)->set_recv_byte_callback(DEVCB_##_recv_byte);

class spi_slave_device : public device_t
{
public:
	// construction/destruction
	spi_slave_device(const machine_config &mconfig, const char *tag,
	           device_t *owner, uint32_t clock );

	template<class _miso> void set_miso_callback(_miso miso) {
		m_write_miso.set_callback(miso);
	}

	template<class _recv_byte>
	void set_recv_byte_callback(_recv_byte recv_byte) {
		m_write_recv_byte.set_callback(recv_byte);
	}

	// Set the byte which is exchanged on the next communication. After the
	// send and receive bytes are exchanged, the next send byte is set to
	// zero.
	void set_next_send_byte(uint8_t);

	DECLARE_WRITE_LINE_MEMBER(write_select);
	DECLARE_WRITE_LINE_MEMBER(write_clock);
	DECLARE_WRITE_LINE_MEMBER(write_mosi);
	DECLARE_READ_LINE_MEMBER(read_miso);

	static void set_mode(device_t &device, spi_mode_t mode) {
		downcast<spi_slave_device &>(device).m_mode = mode;
	}

	static void set_data_direction(device_t &device, spi_data_direction_t data_dir) {
		downcast<spi_slave_device &>(device).m_data_dir = data_dir;
	}

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

private:
	spi_mode_t m_mode;
	spi_data_direction_t m_data_dir;
	bool m_selected;
	int m_clk, m_mosi, m_miso;

	uint8_t m_recv_byte, m_send_byte;
	int m_recv_count, m_send_count;

	devcb_write_line m_write_miso;
	devcb_write8 m_write_recv_byte;

	void clk_edge_(int is_idle_to_active);

	inline void set_miso(int state) { m_miso = state; m_write_miso(m_miso); }

	inline int cpol() {
		return ((m_mode == SPI_MODE2) || (m_mode == SPI_MODE3)) ? 1 : 0;
	}

	inline int cpha() {
		return ((m_mode == SPI_MODE1) || (m_mode == SPI_MODE3)) ? 1 : 0;
	}
};

extern const device_type SPI;

#endif
