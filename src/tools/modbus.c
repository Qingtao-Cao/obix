/*
 * *******************************************************************
 * Copyright (c) 2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This file is part of oBIX.
 *
 * oBIX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * oBIX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with oBIX. If not, see <http://www.gnu.org/licenses/>.
 *
 * *******************************************************************
 */

/*
 * A simple tool to directly access holding registers on a modbus slave
 * and assemble and convert raw data into floats.
 *
 * NOTE: this this program always combines two consecutive uint16_t and
 * converts to a float, it is not suitable to display integer readings.
 *
 * Compile by the follow command:
 *
 *		gcc modbus.c -o modbus -lmodbus
 *
 * Then run in command line as:
 *
 *		./modbus <master ip> <slave id> <addr> <count>
 *
 * Where,
 *	<master ip> - the IP address of modbus master device(with default
 *				  port number 502) such as MGATE box
 *	<slave id>  - the ID of a modbus slave such as a BCM connected on
 *				  one modbus line on a MGATE port
 *	<addr>		- starting register number to read from
 *	<count>		- the amount of consecutive registers, should be an
 *				  even number since each float requires two 16bit
 *				  registers.
 */

#include <modbus/modbus.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/*
 * The default port number used by modbus master
 */
#define MODBUS_MASTER_PORT		502

/*
 * The acceptable delay for reading one register
 */
#define MODBUS_REG_DELAY		1

/*
 * Union to convert uint32_t to float.
 */
typedef union i2f {
	uint32_t i;
	float f;
} i2f_t;

int main(int argc, char *argv[])
{
	modbus_t *ctx;
	int slave, addr, count;
	int rc, i;
	uint16_t *dest;
	struct timeval tv;
	i2f_t u;

	if (argc != 5) {
		printf("Usage: \n\tRead pairs of uint16_t registers from modbus slave"
				"\n\tand convert into floats.\n"
				"\n\t./modbus <master ip> <slave id> <addr> <count>\n\n");
		return -1;
	}

	slave = atoi(argv[2]);
	addr = atoi(argv[3]);
	count = atoi(argv[4]);

	/*
	 * Perform sanity checks on parameters
	 */
	if (count % 2 != 0) {
		printf("count must be even\n");
		return -1;
	}

	if (slave > 247) {
		printf("Invalid slave ID: %d\n", slave);
		return -1;
	}

	if (addr > 9999) {
		printf("Invalid addr: %d\n", addr);
		return -1;
	}

	if (!(dest = (uint16_t *)malloc(count * sizeof(uint16_t)))) {
		printf("Failed to allocate memory\n");
		return -1;
	}

	memset(dest, 0, count * sizeof(uint16_t));

	errno = 0;
	if (!(ctx = modbus_new_tcp(argv[1], MODBUS_MASTER_PORT))) {
		printf("Failed to create modbus ctx: %s", modbus_strerror(errno));
		goto failed;
	}

	errno = 0;
	if (modbus_connect(ctx) < 0) {
		printf("Failed to connect with host %s: %s",
				argv[1], modbus_strerror(errno));
		goto connect_failed;
	}

	errno = 0;
	if (modbus_set_slave(ctx, slave) < 0) {
		printf("Failed to set slave %d: %s", slave, modbus_strerror(errno));
		goto set_slave_failed;
	}

	tv.tv_sec = count * MODBUS_REG_DELAY;
	tv.tv_usec = 0;
	modbus_set_response_timeout(ctx, &tv);

	errno = 0;
	rc = modbus_read_registers(ctx, addr - 1, count, dest);
	if (rc < 0 || rc != count) {
		printf("Failed to read %d 16bit registers from %s, returned %d, %s\n",
				count, addr, rc, modbus_strerror(errno));
		goto set_slave_failed;
	}

	for (i = 0; i < count; i += 2) {
		u.i = (dest[i] << 16) | dest[i + 1];
		printf("\%MF%d: %f", addr + i, u.f);
		if (dest[i] > 0 || dest[i + 1] > 0)
			printf(", MSB: 0x%x, LSB: 0x%x\n",dest[i], dest[i + 1]);
		else
			printf("\n");
	}

	/* Fall through */

set_slave_failed:
	modbus_close(ctx);

connect_failed:
	modbus_free(ctx);

failed:
	free(dest);

	return 0;
}
