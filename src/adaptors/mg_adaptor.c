/* *****************************************************************************
 * Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This file is part of obix-adaptors
 *
 * obix-adaptors is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * obix-adaptors is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obix-adaptors.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <modbus/modbus.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>			/* sprintf */
#include <errno.h>
#include <time.h>			/* time & gmtime_r */
#include <signal.h>
#include <string.h>
#include <float.h>			/* FLT_EPSILON */
#include <math.h>			/* fabsf & isnan */
#include <unistd.h>
#include "obix_client.h"
#include "log_utils.h"
#include "ptask.h"
#include "xml_utils.h"
#include "xml_config.h"

/*
 * XPath predicates to interpret the device configuration file
 */
static const char *XP_IP = "/config/meta/controller_address/ip";
static const char *XP_PORT = "/config/meta/controller_address/port";
static const char *XP_HISTORY_LOBBY = "/config/meta/controller_address/history_lobby";

static const char *XP_COLLECTOR_PERIOD = "/config/meta/mg_collector/period";
static const char *XP_COLLECTOR_SLEEP = "/config/meta/mg_collector/sleep";
static const char *XP_COLLECTOR_MAX_TIMEOUT = "/config/meta/mg_collector/max_timeout";

static const char *XP_UPDATER_PERIOD = "/config/meta/obix_updater/period";
static const char *XP_UPDATER_HISTORY_PERIOD = "/config/meta/obix_updater/history_period";

static const char *XP_CB_PER_PANEL = "/config/meta/misc/cb_per_panel";
static const char *XP_CB_OFFSET = "/config/meta/misc/cb_offset";
static const char *XP_VOLT_L2N_DEF = "/config/meta/misc/volt_l2n_def";
static const char *XP_VOLT_L2L_DEF = "/config/meta/misc/volt_l2l_def";
static const char *XP_PF_DEF = "/config/meta/misc/pf_def";
static const char *XP_AC_FREQ_DEF = "/config/meta/misc/ac_freq_def";
static const char *XP_DELAY_PER_REG = "/config/meta/misc/delay_per_reg";
static const char *XP_CURL_TIMEOUT = "/config/meta/misc/curl_timeout";
static const char *XP_CURL_BULKY = "/config/meta/misc/curl_bulky";

static const char *XP_SN_ADDRESS = "/config/meta/reg_table/sn/address";
static const char *XP_SN_COUNT = "/config/meta/reg_table/sn/count";

static const char *XP_FIRMWARE_ADDRESS = "/config/meta/reg_table/firmware/address";
static const char *XP_FIRMWARE_COUNT = "/config/meta/reg_table/firmware/count";

static const char *XP_MODEL_ADDRESS = "/config/meta/reg_table/model/address";
static const char *XP_MODEL_COUNT = "/config/meta/reg_table/model/count";

static const char *XP_CT_CONFIG_ADDRESS = "/config/meta/reg_table/ct_config/address";
static const char *XP_CT_CONFIG_COUNT = "/config/meta/reg_table/ct_config/count";

static const char *XP_LOCATION_ADDRESS = "/config/meta/reg_table/location/address";
static const char *XP_LOCATION_COUNT = "/config/meta/reg_table/location/count";

static const char *XP_AC_FREQ_ADDRESS = "/config/meta/reg_table/ac_freq/address";
static const char *XP_AC_FREQ_COUNT = "/config/meta/reg_table/ac_freq/count";

static const char *XP_VOLT_L2N_ADDRESS = "/config/meta/reg_table/volt_l2n/address";
static const char *XP_VOLT_L2N_COUNT = "/config/meta/reg_table/volt_l2n/count";

static const char *XP_VOLT_L2L_ADDRESS = "/config/meta/reg_table/volt_l2l/address";
static const char *XP_VOLT_L2L_COUNT = "/config/meta/reg_table/volt_l2l/count";

static const char *XP_VOLT_A_ADDRESS = "/config/meta/reg_table/volt_a/address";
static const char *XP_VOLT_A_COUNT = "/config/meta/reg_table/volt_a/count";

static const char *XP_VOLT_B_ADDRESS = "/config/meta/reg_table/volt_b/address";
static const char *XP_VOLT_B_COUNT = "/config/meta/reg_table/volt_b/count";

static const char *XP_VOLT_C_ADDRESS = "/config/meta/reg_table/volt_c/address";
static const char *XP_VOLT_C_COUNT = "/config/meta/reg_table/volt_c/count";

static const char *XP_TOTAL_KWH_ADDRESS = "/config/meta/reg_table/total_kwh/address";
static const char *XP_TOTAL_KWH_COUNT = "/config/meta/reg_table/total_kwh/count";

static const char *XP_TOTAL_KW_ADDRESS = "/config/meta/reg_table/total_kw/address";
static const char *XP_TOTAL_KW_COUNT = "/config/meta/reg_table/total_kw/count";

static const char *XP_AC_AVG_ADDRESS = "/config/meta/reg_table/ac_avg/address";
static const char *XP_AC_AVG_COUNT = "/config/meta/reg_table/ac_avg/count";

static const char *XP_KWH_ADDRESS = "/config/meta/reg_table/kwh/address";
static const char *XP_KWH_COUNT = "/config/meta/reg_table/kwh/count";

static const char *XP_KW_ADDRESS = "/config/meta/reg_table/kw/address";
static const char *XP_KW_COUNT = "/config/meta/reg_table/kw/count";

static const char *XP_PF_ADDRESS = "/config/meta/reg_table/pf/address";
static const char *XP_PF_COUNT = "/config/meta/reg_table/pf/count";

static const char *XP_I_ADDRESS = "/config/meta/reg_table/I/address";
static const char *XP_I_COUNT = "/config/meta/reg_table/I/count";

static const char *XP_MODBUS = "/config/modbus/list";

static const char *SLAVE_ID = "slave_id";
static const char *PARENT_HREF = "parent_href";


/*
 * By far all types of VerisBCM device can host 84 CBs at most,
 * therefore 2 bits for CB ID will suffice
 */
static const char *BM_NAME_TEMPLATE = "CB%.2d";
static int BM_NAME_LEN = 5;

/*
 * Maximum string length of a 32bit value in hexadecimal format
 */
#define MG_ID_MAX_BITS			8

/*
 * The number of panels hosted on a BCM
 */
#define MG_PANELS_PER_BCM		2

/*
 * The default value of scale register of PF attribute
 */
#define MG_PF_SCALE_DEF			(-3)

/*
 * Union to convert a unsigned 32bit integer to a float
 */
typedef union i2f {
	uint32_t i;
	float f;
} i2f_t;

/*
 * Descriptor of a register table for a consecutive number of
 * registers of a specific physical attribute, such as I, V, PF,
 * KW or KWH, for all BMs on one panel that can be read out
 * altogether in a batch mode.
 */
typedef struct reg_tab {
	/* address of registers */
	int address;

	/* number of registers */
	int count;
} reg_tab_t;

/*
 * Descriptor of the Modbus Gateway(MB for short), which supports
 * a number of modbus lines, each of which further connects with
 * a number of BCM devices.
 *
 * All BCMs so far share the same register tables, so they could
 * be captured in top-level descriptor here.
 */
typedef struct obix_mg {
	/* IP address of the MG box */
	char *ip;

	/* IP port number used by the MB box */
	int port;

	/* History lobby name for all CBs */
	char *history_lobby;

	/* period of mg_collector thread */
	int collector_period;

	/* length of sleep in case of timeout, in seconds */
	int collector_sleep;

	/* maximum allowable timeout */
	int collector_max_timeout;

	/* period of obix_updater thread */
	int updater_period;

	/* period to append history record for each BM */
	int history_period;

	/* default setting read from config file */
	int cb_per_panel;
	int cb_offset;
	float volt_l2n_def;
	float volt_l2l_def;
	float pf_def;
	float ac_freq_def;
	int delay_per_reg;
	int curl_timeout;
	int curl_bulky;

	/* where to read static information of a BCM */
	reg_tab_t sn;
	reg_tab_t firmware;
	reg_tab_t model;
	reg_tab_t ct_config;
	reg_tab_t location;

	/* where to read dynamic information of AUX device of a BCM */
	reg_tab_t ac_freq;
	reg_tab_t volt_l2n;
	reg_tab_t volt_l2l;
	reg_tab_t volt_a;
	reg_tab_t volt_b;
	reg_tab_t volt_c;
	reg_tab_t total_kwh;
	reg_tab_t total_kw;
	reg_tab_t ac_avg;

	/* where to read dynamic information of a BM */
	reg_tab_t kwh;
	reg_tab_t kw;
	reg_tab_t pf;
	reg_tab_t I;

	/* a list of mg_modbus_t */
	struct list_head devices;
} obix_mg_t;

/*
 * Descriptor of a modbus line, a pair of producer-consumer worker
 * threads are created for each modbus line in the hope that various
 * modbus lines could be accessed in parallel.
 *
 * Note,
 * 1. Since CURL handle are not thread safe, therefore each consumer
 * worker thread should grab its own handle to update devices' status
 * and append history records independently from each other.
 */
typedef struct mg_modbus {
	/* pointing to parent descriptor */
	obix_mg_t *p;

	/* port # for debug purpose */
	char *name;

	/* modbus context used by mg_collector */
	modbus_t *ctx;

	/* HistoryAppendIn contract used by obix_updater*/
	char *hist_ain;

	/* CURL handle used by obix_updater */
	CURL_EXT *handle;

	/* producer, to collect latest devices status */
	obix_task_t mg_collector;

	/* consumer, to populate devices status to oBIX srv */
	obix_task_t obix_updater;

	/* joining obix_mg_t.devices */
	struct list_head list;

	/* list of mg_bcm_t */
	struct list_head devices;
} mg_modbus_t;

/*
 * Index and name of float attributes of a BCM(presented by AUX)
 */
typedef enum {
	OBIX_BCM_ATTR_ACFREQ = 0,
	OBIX_BCM_ATTR_VOLTL2N = 1,
	OBIX_BCM_ATTR_VOLTL2L = 2,
	OBIX_BCM_ATTR_VOLTA = 3,
	OBIX_BCM_ATTR_VOLTB = 4,
	OBIX_BCM_ATTR_VOLTC = 5,
	OBIX_BCM_ATTR_TOTALKWH = 6,
	OBIX_BCM_ATTR_TOTALKW = 7,
	OBIX_BCM_ATTR_ACAVG = 8,
	OBIX_BCM_ATTR_TOTALAC = 9,
	OBIX_BCM_ATTR_MAX = 10
} OBIX_BCM_ATTR;

/*
 * Descriptor of a Branch Circuit Meter(BCM for short) device, which
 * consists of two panels with the same amount of BM on each of them.
 *
 * Each panel has a distinct (virutal) slave ID but share the same
 * register tables, which are described in top-level obix_mg_t.
 */
typedef struct mg_bcm {
	/* pointing to parent descriptor */
	mg_modbus_t *p;

	/* device name read from config file */
	char *name;

	/* the root location of this device under the Device Lobby */
	char *parent_href;

	/*
	 * the name that is unique for all BCMs in one datacentre
	 * thus can be used to name its history facility (if needed)
	 */
	char *history_name;

	/* starting slave ID of this device */
	int slave_id;

	/* static raw values read from the AUX device */
	uint16_t *sn_r;
	uint16_t *firmware_r;
	uint16_t *model_r;
	uint16_t *ct_config_r;
	char *location_r;

	/* converted static raw values as integers */
	int sn;
	int firmware;
	int model;
	int ct_config;

	/* dynamic raw values read from the AUX device */
	uint16_t *ac_freq_r;
	uint16_t *volt_l2n_r;
	uint16_t *volt_l2l_r;
	uint16_t *volt_a_r;
	uint16_t *volt_b_r;
	uint16_t *volt_c_r;
	uint16_t *total_kwh_r;
	uint16_t *total_kw_r;
	uint16_t *ac_avg_r;

	/* converted dynamic raw values as floats */
	float attr[OBIX_BCM_ATTR_MAX];

	/* raw values read from one BCM panel */
	uint16_t *kwh_r;
	uint16_t *kw_r;
	uint16_t *pf_r;
	uint16_t *I_r;

	/* last modified time from devices */
	time_t mtime;

	/* last post time to oBIX server */
	time_t rtime;

	/* last update time in timestamp format */
	char *mtime_ts;

	/* flag of whether the device is off-lined */
	int off_line;

	/* for statistics purpose, not used yet */
	int timeout;

	/* for statistics purpose, not used yet */
	int slow_collector;

	/* in procedure of updating from BCM */
	int being_written;

	/* in procedure of posting to oBIX srv */
	int being_read;

	/* waiting queue for reader and writer */
	pthread_cond_t rq, wq;

	/* synchronizing among reader and writer */
	pthread_mutex_t mutex;

	/* joining mg_modbus_t.devices */
	struct list_head list;

	/* lists of BM on each panel */
	struct list_head devices[MG_PANELS_PER_BCM];
} mg_bcm_t;

/*
 * Index and name of power information of a BM
 */
typedef enum {
	OBIX_BM_ATTR_KWH = 0,
	OBIX_BM_ATTR_KW = 1,
	OBIX_BM_ATTR_V = 2,
	OBIX_BM_ATTR_PF = 3,
	OBIX_BM_ATTR_I = 4,
	OBIX_BM_ATTR_MAX = 5
} OBIX_BM_ATTR;

/*
 * Desciptor of a Branch Meter(BM for short) device
 */
typedef struct mg_bm {
	/* pointing to parent descriptor */
	mg_bcm_t *p;

	/* 1-42 on panel1, 43-84 on panel2 */
	int bm_id;

	/*
	 * device name in "CB%.2d" format, used in assembling
	 * the device's href in the device lobby on oBIX server,
	 * such as:
	 *		/obix/deviceRoot/M1/DH1/BCM01/Meters/CB01
	 *
	 * which is determined by OBIX_BM_CONTRACT.
	 */
	char *name;

	/*
	 * the name that is unique for all BMs in one datacentre
	 * thus can be used to name its history facility (if needed)
	 */
	char *history_name;

	/* next time to append history record */
	time_t htime;

	/* physical attributes */
	float attr[OBIX_BM_ATTR_MAX];

	/* joining mg_bcm_t.devices[i] */
	struct list_head list;
} mg_bm_t;

/*
 * Names of the float attributes of a BCM
 */
static const char *mg_bcm_attr[] = {
	[OBIX_BCM_ATTR_ACFREQ] = "ACFreq",
	[OBIX_BCM_ATTR_VOLTL2N] = "VoltL-N",
	[OBIX_BCM_ATTR_VOLTL2L] = "VoltL-L",
	[OBIX_BCM_ATTR_VOLTA] = "VoltA",
	[OBIX_BCM_ATTR_VOLTB] = "VoltB",
	[OBIX_BCM_ATTR_VOLTC] = "VoltC",
	[OBIX_BCM_ATTR_TOTALKWH] = "kWh",
	[OBIX_BCM_ATTR_TOTALKW] = "kW",
	[OBIX_BCM_ATTR_ACAVG] = "CurrentAverage",
	[OBIX_BCM_ATTR_TOTALAC] = "TotalCurrent"
};

/*
 * Names of non-float attributes of a BCM
 */
static const char *MG_BCM_SN = "SerialNumber";
static const char *MG_BCM_FW = "Firmware";
static const char *MG_BCM_MOD = "Model";
static const char *MG_BCM_CTC = "CTConfig";
static const char *MG_BCM_LOC = "Location";
static const char *MG_BCM_MTIME = "LastUpdated";
static const char *MG_BCM_ONLINE = "OnLine";

static const char *MG_BCM_OFFLINED = "DEVICE OFFLINED";

/*
 * oBIX contract of a Veris BCM, the name of each attribute must be
 * the same as those indicated above.
 *
 * Note,
 * 1. Any attribute that needs to be updated at the runtime would
 * have to be declared as writable, or otherwise oBIX server will
 * regard them as read-only.
 *
 * In particular, those static AUX information from serial number
 * to location string would have to be declared as writable as well
 * so that they also can be updated once relevant BCM is brought
 * on-line which used to be disconnected at program start-up.
 */
static const char *OBIX_BCM_CONTRACT =
"<obj name=\"%s\" href=\"/obix/deviceRoot%s%s/\" is=\"nextdc:veris-bcm\">\r\n"
"<int name=\"SlaveID\" href=\"SlaveID\" val=\"%d\"/>\r\n"
"<int name=\"SerialNumber\" href=\"SerialNumber\" val=\"0x%x\" writable=\"true\"/>\r\n"
"<int name=\"Firmware\" href=\"Firmware\" val=\"0x%.8x\" writable=\"true\"/>\r\n"
"<int name=\"Model\" href=\"Model\" val=\"%d\" writable=\"true\"/>\r\n"
"<int name=\"CTConfig\" href=\"CTConfig\" val=\"%d\" writable=\"true\"/>\r\n"
"<str name=\"Location\" href=\"Location\" val=\"%s\" writable=\"true\"/>\r\n"
"<real name=\"ACFreq\" href=\"ACFreq\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"VoltL-N\" href=\"VoltL-N\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"VoltL-L\" href=\"VoltL-L\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"VoltA\" href=\"VoltA\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"VoltB\" href=\"VoltB\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"VoltC\" href=\"VoltC\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"kWh\" href=\"kWh\" displayName=\"Total kWh for 3 phases\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"kW\" href=\"kW\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"CurrentAverage\" href=\"CurrentAverage\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"TotalCurrent\" href=\"TotalCurrent\" val=\"%f\" writable=\"true\"/>\r\n"
"<abstime name=\"LastUpdated\" href=\"LastUpdated\" val=\"%s\" writable=\"true\"/>\r\n"
"<bool name=\"Online\" href=\"OnLine\" val=\"%s\" writable=\"true\"/>\r\n"
"<list name=\"Meters\" href=\"Meters\" of=\"nextdc:Meter\"/>\r\n"
"</obj>\r\n";

static const char *mg_bm_attr[] = {
	[OBIX_BM_ATTR_KWH] = "kWh",
	[OBIX_BM_ATTR_KW] = "kW",
	[OBIX_BM_ATTR_V] = "V",
	[OBIX_BM_ATTR_PF] = "PF",
	[OBIX_BM_ATTR_I] = "I"
};

/*
 * oBIX contract of a BM, the name of each attribute must be
 * the same as those indicated in mg_bm_attr[]
 */
static const char *OBIX_BM_CONTRACT =
"<obj name=\"%s\" href=\"/obix/deviceRoot%s%s/Meters/%s/\" is=\"nextdc:veris-meter\">\r\n"
"<real name=\"kWh\" href=\"kWh\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"kW\" href=\"kW\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"V\" href=\"V\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"PF\" href=\"PF\" val=\"%f\" writable=\"true\"/>\r\n"
"<real name=\"I\" href=\"I\" val=\"%f\" writable=\"true\"/>\r\n"
"</obj>\r\n";

static void mg_collector_task(void *);
static void obix_updater_task(void *);

/*
 * Flags shared between signal handlers and the main thread
 */
static int flag_online_id;
static int flag_exiting;

static inline obix_mg_t *mg_get_mg_modbus(mg_modbus_t *bus)
{
	return bus->p;
}

static inline obix_mg_t *mg_get_mg_bcm(mg_bcm_t *bcm)
{
	return mg_get_mg_modbus(bcm->p);
}

static inline obix_mg_t *mg_get_mg_bm(mg_bm_t *bm)
{
	return mg_get_mg_bcm(bm->p);
}

static void mg_cancel_tasks(obix_mg_t *mg)
{
	mg_modbus_t *bus;

	list_for_each_entry(bus, &mg->devices, list) {
		obix_cancel_task(&bus->mg_collector);
		obix_cancel_task(&bus->obix_updater);

		/*
		 * Free curl handle for obix updater ONLY AFTER
		 * it has been stopped!
		 */
		if (bus->handle)
			curl_ext_free(bus->handle);
	}
}

static int mg_schedule_tasks(obix_mg_t *mg)
{
	mg_modbus_t *bus;
	int error = OBIX_SUCCESS;

	list_for_each_entry(bus, &mg->devices, list) {
		/*
		 * curl handle used by obix_updater can't be created in
		 * mg_setup_modbus() since when that function is invoked
		 * the connection with oBIX server has not been established
		 * yet, in particular, http_init > curl_ext_init not called
		 * yet. That's why the creation has been postponed until
		 * activating obix_updater thread.
		 *
		 * obix_updater only refreshs device status and append history
		 * records, thus does not require a hugh quantum size.
		 */
		error = curl_ext_create(&bus->handle, mg->curl_bulky, mg->curl_timeout);
		if (error != 0) {
			log_error("Failed to create curl handle for %s", bus->name);
			bus->handle = NULL;
			break;
		}

		error = obix_schedule_task(&bus->mg_collector);
		error |= obix_schedule_task(&bus->obix_updater);

		if (error != OBIX_SUCCESS)
			break;
	}

	if (error != OBIX_SUCCESS)
		mg_cancel_tasks(mg);

	return error;
}

/*
 * Read nb consecutive registers(of uint16_t size each) since
 * the addr offset on the specified BCM into dest.
 *
 * Return 0 on success, < 0 otherwise.
 */
static int mg_read_registers(mg_bcm_t *bcm, int addr, int nb, uint16_t *dest)
{
	mg_modbus_t *bus = bcm->p;
	modbus_t *ctx = bus->ctx;
	obix_mg_t *mg = bus->p;
	struct timeval tv;
	int rc;

	assert(nb > 0);

	/*
	 * The allowable timeout threshold is decided by the
	 * number of registers to read
	 */
	tv.tv_sec = mg->delay_per_reg * nb;
	tv.tv_usec = 0;
	modbus_set_response_timeout(ctx, &tv);

	/*
	 * Manually initialize the destination region or otherwise
	 * libmodbus may return garbage
	 */
	memset(dest, 0, nb * sizeof(uint16_t));

	/*
	 * libmodbus APIs expect register numbers starting from zero,
	 * that is, the value specified in config file deducts 1.
	 */
	errno = 0;
	rc = modbus_read_registers(ctx, addr - 1, nb, dest);
	if (rc < 0 || rc != nb) {
		log_error("Failed to read %d regs from %d on BCM %s, returned %d: %s",
					nb, addr, bcm->name, rc, modbus_strerror(errno));
		return -1;
	} else
		return 0;
}

static void mg_destroy_param(obix_mg_t *mg)
{
	if (!mg) {
		return;
	}

	if (mg->ip) {
		free(mg->ip);
	}

	if (mg->history_lobby) {
		free(mg->history_lobby);
	}
}

/*
 * Read all meta settings of the MG box from its config file
 */
static int mg_setup_param(obix_mg_t *mg, xml_config_t *config)
{

	if (!(mg->ip = xml_config_get_str(config, XP_IP)) ||
		!(mg->history_lobby = xml_config_get_str(config, XP_HISTORY_LOBBY)) ||
		(mg->port = xml_config_get_int(config, XP_PORT)) < 0 ||
		(mg->collector_period = xml_config_get_int(config, XP_COLLECTOR_PERIOD)) < 0 ||
		(mg->collector_sleep = xml_config_get_int(config, XP_COLLECTOR_SLEEP)) < 0  ||
		(mg->collector_max_timeout = xml_config_get_int(config, XP_COLLECTOR_MAX_TIMEOUT)) < 0 ||
		(mg->updater_period = xml_config_get_int(config, XP_UPDATER_PERIOD)) < 0 ||
		(mg->history_period = xml_config_get_int(config, XP_UPDATER_HISTORY_PERIOD)) < 0 ||
		(mg->cb_per_panel = xml_config_get_int(config, XP_CB_PER_PANEL)) < 0 ||
		(mg->cb_offset = xml_config_get_int(config, XP_CB_OFFSET)) < 0 ||
		xml_config_get_float(config, XP_VOLT_L2N_DEF, &mg->volt_l2n_def) < 0 ||
		xml_config_get_float(config, XP_VOLT_L2L_DEF, &mg->volt_l2l_def) < 0 ||
		xml_config_get_float(config, XP_PF_DEF, &mg->pf_def) < 0 ||
		xml_config_get_float(config, XP_AC_FREQ_DEF, &mg->ac_freq_def) < 0 ||
		(mg->delay_per_reg = xml_config_get_int(config, XP_DELAY_PER_REG)) < 0 ||
		(mg->curl_timeout = xml_config_get_int(config, XP_CURL_TIMEOUT)) < 0 ||
		(mg->curl_bulky = xml_config_get_int(config, XP_CURL_BULKY)) < 0 ||
		(mg->sn.address = xml_config_get_int(config, XP_SN_ADDRESS)) < 0 ||
		(mg->sn.count = xml_config_get_int(config, XP_SN_COUNT)) < 0 ||
		(mg->firmware.address = xml_config_get_int(config, XP_FIRMWARE_ADDRESS)) < 0 ||
		(mg->firmware.count = xml_config_get_int(config, XP_FIRMWARE_COUNT)) < 0 ||
		(mg->model.address = xml_config_get_int(config, XP_MODEL_ADDRESS)) < 0 ||
		(mg->model.count = xml_config_get_int(config, XP_MODEL_COUNT)) < 0 ||
		(mg->ct_config.address = xml_config_get_int(config, XP_CT_CONFIG_ADDRESS)) < 0 ||
		(mg->ct_config.count = xml_config_get_int(config, XP_CT_CONFIG_COUNT)) < 0 ||
		(mg->location.address = xml_config_get_int(config, XP_LOCATION_ADDRESS)) < 0 ||
		(mg->location.count = xml_config_get_int(config, XP_LOCATION_COUNT)) < 0 ||
		(mg->ac_freq.address = xml_config_get_int(config, XP_AC_FREQ_ADDRESS)) < 0 ||
		(mg->ac_freq.count = xml_config_get_int(config, XP_AC_FREQ_COUNT)) < 0 ||
		(mg->volt_l2n.address = xml_config_get_int(config, XP_VOLT_L2N_ADDRESS)) < 0 ||
		(mg->volt_l2n.count = xml_config_get_int(config, XP_VOLT_L2N_COUNT)) < 0 ||
		(mg->volt_l2l.address = xml_config_get_int(config, XP_VOLT_L2L_ADDRESS)) < 0 ||
		(mg->volt_l2l.count = xml_config_get_int(config, XP_VOLT_L2L_COUNT)) < 0 ||
		(mg->volt_a.address = xml_config_get_int(config, XP_VOLT_A_ADDRESS)) < 0 ||
		(mg->volt_a.count = xml_config_get_int(config, XP_VOLT_A_COUNT)) < 0 ||
		(mg->volt_b.address = xml_config_get_int(config, XP_VOLT_B_ADDRESS)) < 0 ||
		(mg->volt_b.count = xml_config_get_int(config, XP_VOLT_B_COUNT)) < 0 ||
		(mg->volt_c.address = xml_config_get_int(config, XP_VOLT_C_ADDRESS)) < 0 ||
		(mg->volt_c.count = xml_config_get_int(config, XP_VOLT_C_COUNT)) < 0 ||
		(mg->total_kwh.address = xml_config_get_int(config, XP_TOTAL_KWH_ADDRESS)) < 0 ||
		(mg->total_kwh.count = xml_config_get_int(config, XP_TOTAL_KWH_COUNT)) < 0 ||
		(mg->total_kw.address = xml_config_get_int(config, XP_TOTAL_KW_ADDRESS)) < 0 ||
		(mg->total_kw.count = xml_config_get_int(config, XP_TOTAL_KW_COUNT)) < 0 ||
		(mg->ac_avg.address = xml_config_get_int(config, XP_AC_AVG_ADDRESS)) < 0 ||
		(mg->ac_avg.count = xml_config_get_int(config, XP_AC_AVG_COUNT)) < 0 ||
		(mg->kwh.address = xml_config_get_int(config, XP_KWH_ADDRESS)) < 0 ||
		(mg->kwh.count = xml_config_get_int(config, XP_KWH_COUNT)) < 0 ||
		(mg->kw.address = xml_config_get_int(config, XP_KW_ADDRESS)) < 0 ||
		(mg->kw.count = xml_config_get_int(config, XP_KW_COUNT)) < 0 ||
		(mg->pf.address = xml_config_get_int(config, XP_PF_ADDRESS)) < 0 ||
		(mg->pf.count = xml_config_get_int(config, XP_PF_COUNT)) < 0 ||
		(mg->I.address = xml_config_get_int(config, XP_I_ADDRESS)) < 0 ||
		(mg->I.count = xml_config_get_int(config, XP_I_COUNT)) < 0) {
		log_error("Failed to get configurables");
		goto failed;
	}

	/*
	 * Adjust the start address of registers based on the number
	 * of CBs skipped over at the beginning of each panel
	 *
	 * NOTE: each register occupies 2 uint16_t readings
	 */
	if (mg->cb_offset > 0) {
		mg->cb_per_panel -= mg->cb_offset;

		mg->kwh.address += mg->cb_offset * 2;
		mg->kwh.count -= mg->cb_offset * 2;

		mg->kw.address += mg->cb_offset * 2;
		mg->kw.count -= mg->cb_offset * 2;

		mg->pf.address += mg->cb_offset;
		mg->pf.count -= mg->cb_offset;

		mg->I.address += mg->cb_offset * 2;
		mg->I.count -= mg->cb_offset * 2;
	}

	return OBIX_SUCCESS;

failed:
	mg_destroy_param(mg);
	return OBIX_ERR_INVALID_ARGUMENT;
}

static void mg_destroy_bm(mg_bm_t *bm)
{
	if (!bm) {
		return;
	}

	if (bm->history_name) {
		free(bm->history_name);
	}

	if (bm->name)  {
		free(bm->name);
	}

	free(bm);
}

static int mg_setup_bm(int panel_id, int index, mg_bcm_t *bcm)
{
	obix_mg_t *mg = mg_get_mg_bcm(bcm);
	mg_bm_t *bm;
	char buf[BM_NAME_LEN];
	int ret = OBIX_ERR_NO_MEMORY;

	if (!(bm = (mg_bm_t *)malloc(sizeof(mg_bm_t)))) {
		log_error("Failed to allocate BM descriptor");
		return OBIX_ERR_NO_MEMORY;
	}

	memset(bm, 0, sizeof(mg_bm_t));
	bm->p = bcm;
	bm->htime = time(NULL) + mg->history_period;

	/*
	 * BM device ID range:
	 *	Panel #1: [1, 42]
	 *	Panel #2: [43, 84]
	 */
	bm->bm_id = mg->cb_per_panel * panel_id + index + 1;
	INIT_LIST_HEAD(&bm->list);

	sprintf(buf, BM_NAME_TEMPLATE, bm->bm_id);
	if (!(bm->name = strdup(buf))) {
		log_error("Failed to allocate BM name for CB%.2d", bm->bm_id);
		goto failed;
	}

	if (link_pathname(&bm->history_name, mg->history_lobby,
					  bcm->name, bm->name, NULL) < 0) {
		log_error("Failed to assemble history name for CB%.2d", bm->bm_id);
		goto failed;
	}

	list_add_tail(&bm->list, &bcm->devices[panel_id]);
	return OBIX_SUCCESS;

failed:
	mg_destroy_bm(bm);
	return ret;
}

static void mg_destroy_bcm_regs(mg_bcm_t *bcm)
{
	if (bcm->sn_r)
		free(bcm->sn_r);
	if (bcm->firmware_r)
		free(bcm->firmware_r);
	if (bcm->model_r)
		free(bcm->model_r);
	if (bcm->ct_config_r)
		free(bcm->ct_config_r);
	if (bcm->location_r)
		free(bcm->location_r);

	if (bcm->ac_freq_r)
		free(bcm->ac_freq_r);
	if (bcm->volt_l2n_r)
		free(bcm->volt_l2n_r);
	if (bcm->volt_l2l_r)
		free(bcm->volt_l2l_r);
	if (bcm->volt_a_r)
		free(bcm->volt_a_r);
	if (bcm->volt_b_r)
		free(bcm->volt_b_r);
	if (bcm->volt_c_r)
		free(bcm->volt_c_r);
	if (bcm->total_kwh_r)
		free(bcm->total_kwh_r);
	if (bcm->total_kw_r)
		free(bcm->total_kw_r);
	if (bcm->ac_avg_r)
		free(bcm->ac_avg_r);

	if (bcm->kwh_r)
		free(bcm->kwh_r);
	if (bcm->kw_r)
		free(bcm->kw_r);
	if (bcm->pf_r)
		free(bcm->pf_r);
	if (bcm->I_r)
		free(bcm->I_r);
}

/*
 * Allocate buffers for every register tables of a BCM
 *
 * Note,
 * 1. According to "E30 SERIES MODBUS POINT MAP" p3, the
 * location string will already be NULL terminated.
 */
static int mg_setup_bcm_regs(mg_bcm_t *bcm)
{
	obix_mg_t *mg = mg_get_mg_bcm(bcm);

	bcm->sn_r = (uint16_t *)malloc(mg->sn.count * sizeof(uint16_t));
	bcm->firmware_r = (uint16_t *)malloc(mg->firmware.count * sizeof(uint16_t));
	bcm->model_r = (uint16_t *)malloc(mg->model.count * sizeof(uint16_t));
	bcm->ct_config_r = (uint16_t *)malloc(mg->ct_config.count * sizeof(uint16_t));
	bcm->location_r = (char *)malloc(mg->location.count * sizeof(uint16_t));

	bcm->ac_freq_r = (uint16_t *)malloc(mg->ac_freq.count * sizeof(uint16_t));
	bcm->volt_l2n_r = (uint16_t *)malloc(mg->volt_l2n.count * sizeof(uint16_t));
	bcm->volt_l2l_r = (uint16_t *)malloc(mg->volt_l2l.count * sizeof(uint16_t));
	bcm->volt_a_r = (uint16_t *)malloc(mg->volt_a.count * sizeof(uint16_t));
	bcm->volt_b_r = (uint16_t *)malloc(mg->volt_b.count * sizeof(uint16_t));
	bcm->volt_c_r = (uint16_t *)malloc(mg->volt_c.count * sizeof(uint16_t));
	bcm->total_kwh_r = (uint16_t *)malloc(mg->total_kwh.count * sizeof(uint16_t));
	bcm->total_kw_r = (uint16_t *)malloc(mg->total_kw.count * sizeof(uint16_t));
	bcm->ac_avg_r = (uint16_t *)malloc(mg->ac_avg.count * sizeof(uint16_t));

	bcm->kwh_r = (uint16_t *)malloc(mg->kwh.count * sizeof(uint16_t));
	bcm->kw_r = (uint16_t *)malloc(mg->kw.count * sizeof(uint16_t));
	bcm->pf_r = (uint16_t *)malloc(mg->pf.count * sizeof(uint16_t));
	bcm->I_r = (uint16_t *)malloc(mg->I.count * sizeof(uint16_t));

	if (!bcm->sn_r ||
		!bcm->firmware_r ||
		!bcm->model_r ||
		!bcm->ct_config_r ||
		!bcm->location_r ||
		!bcm->ac_freq_r ||
		!bcm->volt_l2n_r ||
		!bcm->volt_l2l_r ||
		!bcm->volt_a_r ||
		!bcm->volt_b_r ||
		!bcm->volt_c_r ||
		!bcm->total_kwh_r ||
		!bcm->total_kw_r ||
		!bcm->ac_avg_r ||
		!bcm->kwh_r ||
		!bcm->kw_r ||
		!bcm->pf_r ||
		!bcm->I_r) {
		return OBIX_ERR_NO_MEMORY;
	} else {
		return OBIX_SUCCESS;
	}
}

static void mg_destroy_bcm(mg_bcm_t *bcm)
{
	mg_bm_t *bm, *n;
	int i;

	if (!bcm) {
		return;
	}

	for (i = 0; i < MG_PANELS_PER_BCM; i++) {
		list_for_each_entry_safe(bm, n, &bcm->devices[i], list) {
			list_del(&bm->list);
			mg_destroy_bm(bm);
		}
	}

	if (bcm->name) {
		free(bcm->name);
	}

	if (bcm->history_name) {
		free(bcm->history_name);
	}

	if (bcm->mtime_ts) {
		free(bcm->mtime_ts);
	}

	if (bcm->parent_href) {
		free(bcm->parent_href);
	}

	mg_destroy_bcm_regs(bcm);

	pthread_mutex_destroy(&bcm->mutex);
	pthread_cond_destroy(&bcm->rq);
	pthread_cond_destroy(&bcm->wq);

	free(bcm);
}

static int mg_setup_bcm(xmlNode *node, mg_modbus_t *bus)
{
	obix_mg_t *mg = bus->p;
	mg_bcm_t *bcm;
	int i, j;
	int ret = OBIX_ERR_NO_MEMORY;

	if (!(bcm = (mg_bcm_t *)malloc(sizeof(mg_bcm_t)))) {
		log_error("Failed to allocate BCM descriptor");
		return OBIX_ERR_NO_MEMORY;
	}
	memset(bcm, 0, sizeof(mg_bcm_t));

	bcm->p = bus;

	if (!(bcm->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME))) {
		log_error("Failed to get name attr from current BCM node");
		goto failed;
	}

	if (link_pathname(&bcm->history_name, mg->history_lobby, NULL,
					  bcm->name, NULL) < 0) {
		log_error("Failed to assemble history name for BCM %s", bcm->name);
		goto failed;
	}

	if ((bcm->slave_id = xml_get_child_long(node, OBIX_OBJ_INT,
											SLAVE_ID)) < 0) {
		log_error("Failed to get %s from current BCM node", SLAVE_ID);
		goto failed;
	}


	if (!(bcm->parent_href = xml_get_child_val(node, OBIX_OBJ_STR,
											   PARENT_HREF))) {
		log_error("Failed to get %s from current BCM node", PARENT_HREF);
		goto failed;
	}

	if ((ret = mg_setup_bcm_regs(bcm)) != OBIX_SUCCESS) {
		log_error("Failed to allocate registers buffer");
		goto failed;
	}

	pthread_mutex_init(&bcm->mutex, NULL);
	pthread_cond_init(&bcm->rq, NULL);
	pthread_cond_init(&bcm->wq, NULL);

	INIT_LIST_HEAD(&bcm->list);

	for (i = 0; i < MG_PANELS_PER_BCM; i++) {
		INIT_LIST_HEAD(&bcm->devices[i]);

		for (j = 0; j < mg->cb_per_panel; j++) {
			if ((ret = mg_setup_bm(i, j, bcm)) != OBIX_SUCCESS) {
				log_error("Failed to setup BM descriptor");
				goto failed;
			}
		}
	}

	list_add_tail(&bcm->list, &bus->devices);

	return OBIX_SUCCESS;

failed:
	mg_destroy_bcm(bcm);
	return ret;
}

/*
 * Release a modbus descriptor and its direct descendants
 */
static void mg_destroy_modbus(mg_modbus_t *bus)
{
	mg_bcm_t *bcm, *n;

	if (!bus) {
		return;
	}

	list_for_each_entry_safe(bcm, n, &bus->devices, list) {
		list_del(&bcm->list);
		mg_destroy_bcm(bcm);
	}

	obix_destroy_task(&bus->mg_collector);
	obix_destroy_task(&bus->obix_updater);

	if (bus->ctx) {
		modbus_close(bus->ctx);
		modbus_free(bus->ctx);
	}

	if (bus->name) {
		free(bus->name);
	}

	if (bus->hist_ain) {
		free(bus->hist_ain);
	}

	free(bus);
}

/*
 * Setup descriptors on a single modbus line according to its
 * config settings.
 *
 * Return the address of created mg_modbus_t descriptor on success,
 * NULL otherwise.
 */
static int mg_setup_modbus(xmlNode *node, void *arg1, void *arg2)
{
	obix_mg_t *mg = (obix_mg_t *)arg1;	/* arg2 ignored */
	mg_modbus_t *bus;
	xmlNode *item;
	int ret = OBIX_ERR_NO_MEMORY;

	if (!(bus = (mg_modbus_t *)malloc(sizeof(mg_modbus_t)))) {
		log_error("Failed to allocate mg_modbus_t");
		return OBIX_ERR_NO_MEMORY;
	}
	memset(bus, 0, sizeof(mg_modbus_t));

	if (!(bus->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME))) {
		log_error("Failed to allocate modbus name");
		goto failed;
	}

	bus->p = mg;
	INIT_LIST_HEAD(&bus->list);
	INIT_LIST_HEAD(&bus->devices);

	errno = 0;
	if (!(bus->ctx = modbus_new_tcp(mg->ip, mg->port))) {
		log_error("Failed to setup modbus ctx: %s", modbus_strerror(errno));
		ret = OBIX_ERR_BAD_CONNECTION_HW;
		goto failed;
	}

	errno = 0;
	if (modbus_connect(bus->ctx) < 0) {
		log_error("Failed to connect with MG: %s", modbus_strerror(errno));
		ret = OBIX_ERR_BAD_CONNECTION_HW;
		goto failed;
	}

	if (obix_setup_task(&bus->mg_collector, NULL, mg_collector_task, bus,
						bus->p->collector_period, EXECUTE_INDEFINITE) < 0) {
		log_error("Failed to create mg_collector thread");
		goto failed;
	}

	if (obix_setup_task(&bus->obix_updater, NULL, obix_updater_task, bus,
						bus->p->updater_period, EXECUTE_INDEFINITE) < 0) {
		log_error("Failed to create obix_updater thread");
		goto failed;
	}

	for (item = node->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST OBIX_OBJ) != 0) {
			continue;
		}

		if ((ret = mg_setup_bcm(item, bus)) != OBIX_SUCCESS) {
			log_error("Failed to setup BCM descriptor on Modbus %s",
					  bus->name);
			goto failed;
		}
	}

	list_add_tail(&bus->list, &mg->devices);
	return OBIX_SUCCESS;

failed:
	mg_destroy_modbus(bus);
	return ret;
}

/*
 * Release MG desriptor and its direct descendant
 */
static void mg_destroy_mg(obix_mg_t *mg)
{
	mg_modbus_t *bus, *n;

	if (!mg) {
		return;
	}

	list_for_each_entry_safe(bus, n, &mg->devices, list) {
		list_del(&bus->list);
		mg_destroy_modbus(bus);
	}

	mg_destroy_param(mg);

	free(mg);
}

/*
 * Setup descriptors on different levels according to device's
 * configuration file.
 *
 * Return the address of an obix_mg_t structure on success,
 * NULL otherwise.
 */
static obix_mg_t *mg_setup_mg(const char *dev_config)
{
	xml_config_t *config;
	obix_mg_t *mg;

	if (!(config = xml_config_create(NULL, dev_config))) {
		log_error("%s is not a valid XML file", dev_config);
		return NULL;
	}

	if (!(mg = (obix_mg_t *)malloc(sizeof(obix_mg_t)))) {
		log_error("Failed to malloc ctrl descriptor");
		goto failed;
	}

	memset(mg, 0, sizeof(obix_mg_t));
	INIT_LIST_HEAD(&mg->devices);

	if (mg_setup_param(mg, config) != OBIX_SUCCESS) {
		log_error("Failed to setup parameter");
		goto failed;
	}

	if (xml_config_for_each_obj(config, XP_MODBUS, mg_setup_modbus,
								mg, NULL) != OBIX_SUCCESS) {
		log_error("Failed to setup modbus descriptor");
		goto failed;
	}

	xml_config_free(config);

	return mg;

failed:
	xml_config_free(config);
	mg_destroy_mg(mg);

	return NULL;
}

static void mg_unregister_bm(mg_bm_t *bm)
{
	obix_unregister_device(OBIX_CONNECTION_ID, bm->history_name);

	/* History records would always be preserved */
}

/*
 * Register a BM to oBIX server and have a history facility
 * setup for it
 *
 * Return 0 OBIX_SUCCESS on success, < 0 on errors.
 */
static int mg_register_bm(mg_bm_t *bm)
{
	mg_bcm_t *bcm = bm->p;
	char *dev_data;
	int len, ret;

	len = strlen(OBIX_BM_CONTRACT) + strlen(bm->name) * 2 +
			strlen(bcm->parent_href) + strlen(bcm->name) - 8 +
			(FLOAT_MAX_BITS - 2) * OBIX_BM_ATTR_MAX;

	if (!(dev_data = (char *)malloc(len + 1))) {
		log_error("Failed to allocate BM contract");
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(dev_data, OBIX_BM_CONTRACT,
			bm->name, bcm->parent_href, bcm->name, bm->name,
			bm->attr[OBIX_BM_ATTR_KWH],
			bm->attr[OBIX_BM_ATTR_KW],
			bm->attr[OBIX_BM_ATTR_V],
			bm->attr[OBIX_BM_ATTR_PF],
			bm->attr[OBIX_BM_ATTR_I]);

	ret = obix_register_device(OBIX_CONNECTION_ID, bm->history_name, dev_data);
	free(dev_data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register BM %s", bm->name);
		return ret;
	}

	ret = obix_get_history(NULL, OBIX_CONNECTION_ID, bm->history_name);
	if (ret != OBIX_SUCCESS) {
		log_error("Failed to create a history facility for %s", bm->name);
		mg_unregister_bm(bm);
	}

	return ret;
}

static void mg_unregister_bcm(mg_bcm_t *bcm)
{
	mg_bm_t *bm;
	int i;

	for (i = 0; i < MG_PANELS_PER_BCM; i++) {
		list_for_each_entry(bm, &bcm->devices[i], list) {
			mg_unregister_bm(bm);
		}
	}

	obix_unregister_device(OBIX_CONNECTION_ID, bcm->history_name);
}

/*
 * Read register tables about a BCM's static information from
 * the on-board AUX device.
 *
 * Return 0 on success, < 0 otherwise
 */
static int mg_collect_aux_static(mg_bcm_t *bcm)
{
	mg_modbus_t *bus = bcm->p;
	modbus_t *ctx = bus->ctx;
	obix_mg_t *mg = bus->p;
	int rc;

	errno = 0;
	rc = modbus_set_slave(ctx, bcm->slave_id);
	if (rc < 0) {
		log_error("Failed to set BCM %s slave_id %d to modbus ctx: %s",
					bcm->name, bcm->slave_id, modbus_strerror(errno));
		return -1;
	}

	/*
	 * Firstly, read AUX registers' raw data
	 */
	rc = mg_read_registers(bcm, mg->sn.address, mg->sn.count, bcm->sn_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->firmware.address, mg->firmware.count,
							bcm->firmware_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->model.address, mg->model.count,
							bcm->model_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->ct_config.address, mg->ct_config.count,
							bcm->ct_config_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->location.address, mg->location.count,
							(uint16_t *)bcm->location_r);
	if (rc < 0)
		return -1;

	/*
	 * Secondly, convert big-endian raw data into 32bit integer
	 */
	bcm->sn = (bcm->sn_r[0] << 16) | bcm->sn_r[1];
	bcm->firmware = (bcm->firmware_r[0] << 16) | bcm->firmware_r[1];
	bcm->model = bcm->model_r[0];
	bcm->ct_config = bcm->ct_config_r[0];

	return 0;
}

/*
 * Register a BCM device and all hosted BM devices to oBIX server.
 *
 * NOTE: on registration, the dynamic data read from the AUX device
 * on the BCM would temporarily be zero, and they should be updated
 * by obix_updater thread in a short notice. However, if the collector
 * thread fails to read from relevant modbus controller, their values
 * will remain zero
 */
static int mg_register_bcm(obix_mg_t *mg, mg_bcm_t *bcm)
{
	mg_bm_t *bm;
	char *dev_data, *location;
	int i, len, ret;

	/*
	 * TODO:
	 * Invoke some client side library API to check if the device
	 * has been registered already, perhaps by the previous instance
	 * of the adapter program, by checking if relevant reference node
	 * exists in the device lobby or not
	 */

	/*
	 * If current BCM is off-lined at start-up, mark it as it is
	 * instead of return error to have the adapter program bail out
	 * eventually.
	 *
	 * System administrator can send SIGUSR1 signal with the BCM's
	 * virtual slave ID to bring it on-line after connected. Please
	 * refer to comments to mg_resurrect_dev for more information.
	 */
	if (mg_collect_aux_static(bcm) < 0) {
		bcm->off_line = 1;
		*bcm->location_r = '\0';	/* Empty location string */
	}

	len = strlen(OBIX_BCM_CONTRACT) + (strlen(bcm->name) - 2) * 2 + strlen(bcm->parent_href) - 2 +
			(MG_ID_MAX_BITS - 2) * 5 +
			(((strlen(bcm->location_r) > 0) ? strlen(bcm->location_r) : strlen(MG_BCM_OFFLINED)) - 2) +
			(FLOAT_MAX_BITS - 2) * OBIX_BCM_ATTR_MAX +
			HIST_REC_TS_MAX_LEN - 2 + sizeof(XML_BOOL_MAX_LEN) - 2;

	if (!(dev_data = (char *)malloc(len + 1))) {
		log_error("Failed to allocate BCM contract");
		return OBIX_ERR_NO_MEMORY;
	}

	/*
	 * Handle special cases that the location string is either empty,
	 * or enclosed by quotation marks already. If that is the case,
	 * then convert quotation marks to space character
	 */
	if ((len = strlen(bcm->location_r)) > 0) {
		for (i = 0; i < len; i++) {
			if (*(bcm->location_r + i) == '\"') {
				*(bcm->location_r + i) = ' ';
			}
		}
		location = bcm->location_r;
	} else {
		location = (char *)MG_BCM_OFFLINED;
	}

	sprintf(dev_data, OBIX_BCM_CONTRACT,
			bcm->name, bcm->parent_href, bcm->name,
			bcm->slave_id,
			bcm->sn,
			bcm->firmware,
			bcm->model,
			bcm->ct_config,
			location,
			bcm->attr[OBIX_BCM_ATTR_ACFREQ],
			bcm->attr[OBIX_BCM_ATTR_VOLTL2N],
			bcm->attr[OBIX_BCM_ATTR_VOLTL2L],
			bcm->attr[OBIX_BCM_ATTR_VOLTA],
			bcm->attr[OBIX_BCM_ATTR_VOLTB],
			bcm->attr[OBIX_BCM_ATTR_VOLTC],
			bcm->attr[OBIX_BCM_ATTR_TOTALKWH],
			bcm->attr[OBIX_BCM_ATTR_TOTALKW],
			bcm->attr[OBIX_BCM_ATTR_ACAVG],
			bcm->attr[OBIX_BCM_ATTR_TOTALAC],
			(bcm->mtime_ts != NULL) ? bcm->mtime_ts : HIST_TS_INIT,
			(bcm->off_line == 0) ? XML_TRUE : XML_FALSE);

	ret = obix_register_device(OBIX_CONNECTION_ID, bcm->history_name, dev_data);
	free(dev_data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register BCM %s", bcm->name);
		return ret;
	}

	for (i = 0; i < MG_PANELS_PER_BCM; i++) {
		list_for_each_entry(bm, &bcm->devices[i], list) {
			if ((ret = mg_register_bm(bm)) != OBIX_SUCCESS) {
				break;
			}
		}
	}

	if (ret != OBIX_SUCCESS) {
		mg_unregister_bcm(bcm);
	}

	return ret;
}

/*
 * Unregister each BM device to oBIX server
 */
static void mg_unregister_devices(obix_mg_t *mg)
{
	mg_modbus_t *bus;
	mg_bcm_t *bcm;

	list_for_each_entry(bus, &mg->devices, list) {
		list_for_each_entry(bcm, &bus->devices, list) {
			mg_unregister_bcm(bcm);
		}
	}
}

/*
 * Register each BCM with onboard BM to oBIX server
 *
 * Return OBIX_SUCCESS on success, < 0 otherwise
 *
 * Note,
 * 1. On failure to register a BCM(with accompanied all BM on it),
 * we can't simply mark it as off-line since they have not been
 * registered to oBIX server therefore obix_updater thread can't
 * play with them at all.
 *
 * 2. On failure to register a BCM(with accompanied all BM on it),
 * we also can't tell if the reason is that the device has been
 * registered before, in which case we should continue registering
 * the rest of devices.
 *
 * In theory, when adapter exits it should unregister all its devices,
 * however, unfortunately, oBIX server doesn't support device unregistration
 * yet, adapter would have to ignore registration failures in order
 * to able to re-run.
 *
 * Return OBIX_SUCCESS on success, < 0 on errors.
 */
static int mg_register_devices(obix_mg_t *mg)
{
	mg_modbus_t *bus;
	mg_bcm_t *bcm;
	int ret = OBIX_SUCCESS;

	list_for_each_entry(bus, &mg->devices, list) {
		log_debug("Register devices on modbus %s", bus->name);
		list_for_each_entry(bcm, &bus->devices, list) {
			if ((ret = mg_register_bcm(mg, bcm)) != OBIX_SUCCESS) {
				break;
			}
		}
	}

	if (ret != OBIX_SUCCESS) {
		mg_unregister_devices(mg);
	}

	return ret;
}

/*
 * Read register tables about dynamic BCM information from
 * the on-board AUX device, then assemble and convert raw data
 * into floats.
 *
 * Return 0 on success, < 0 otherwise
 */
static int mg_collect_aux(mg_bcm_t *bcm)
{
	modbus_t *ctx = bcm->p->ctx;
	obix_mg_t *mg = mg_get_mg_bcm(bcm);
	int rc;
	i2f_t u;

	errno = 0;
	if (modbus_set_slave(ctx, bcm->slave_id) < 0) {
		log_error("Failed to set BCM %s slave_id %d to modbus ctx: %s",
					bcm->name, bcm->slave_id, modbus_strerror(errno));
		return -1;
	}

	/*
	 * Firstly, read AUX registers' raw data
	 */
	rc = mg_read_registers(bcm, mg->ac_freq.address, mg->ac_freq.count,
							bcm->ac_freq_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->volt_l2n.address, mg->volt_l2n.count,
							bcm->volt_l2n_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->volt_l2l.address, mg->volt_l2l.count,
							bcm->volt_l2l_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->volt_a.address, mg->volt_a.count,
							bcm->volt_a_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->volt_b.address, mg->volt_b.count,
							bcm->volt_b_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->volt_c.address, mg->volt_c.count,
							bcm->volt_c_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->total_kwh.address, mg->total_kwh.count,
							bcm->total_kwh_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->total_kw.address, mg->total_kw.count,
							bcm->total_kw_r);
	if (rc < 0)
		return -1;

	rc = mg_read_registers(bcm, mg->ac_avg.address, mg->ac_avg.count,
							bcm->ac_avg_r);
	if (rc < 0)
		return -1;

	/*
	 * Secondly, assemble two uint16_t raw data into one uint32_t and
	 * then convert it into a float.
	 *
	 * Note,
	 * 1. A float value should compare with FLT_EPSILON to check
	 * if it is indeed "zero" and there is also a chance that it is
	 * not a number at all.
	 */
	u.i = (bcm->ac_freq_r[0] << 16) | bcm->ac_freq_r[1];
	bcm->attr[OBIX_BCM_ATTR_ACFREQ] = u.f;
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_ACFREQ]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_ACFREQ]) > 0))
		bcm->attr[OBIX_BCM_ATTR_ACFREQ] = mg->ac_freq_def;

	u.i = (bcm->volt_l2n_r[0] << 16) | bcm->volt_l2n_r[1];
	bcm->attr[OBIX_BCM_ATTR_VOLTL2N] = u.f;
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_VOLTL2N]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_VOLTL2N]) > 0))
		bcm->attr[OBIX_BCM_ATTR_VOLTL2N] = mg->volt_l2n_def;

	u.i = (bcm->volt_l2l_r[0] << 16) | bcm->volt_l2l_r[1];
	bcm->attr[OBIX_BCM_ATTR_VOLTL2L] = u.f;
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_VOLTL2L]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_VOLTL2L]) > 0))
		bcm->attr[OBIX_BCM_ATTR_VOLTL2L] = mg->volt_l2l_def;

	u.i = (bcm->volt_a_r[0] << 16) | bcm->volt_a_r[1];
	bcm->attr[OBIX_BCM_ATTR_VOLTA] = u.f;
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_VOLTA]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_VOLTA]) > 0))
		bcm->attr[OBIX_BCM_ATTR_VOLTA] = mg->volt_l2n_def;

	u.i = (bcm->volt_b_r[0] << 16) | bcm->volt_b_r[1];
	bcm->attr[OBIX_BCM_ATTR_VOLTB] = u.f;
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_VOLTB]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_VOLTB]) > 0))
		bcm->attr[OBIX_BCM_ATTR_VOLTB] = mg->volt_l2n_def;

	u.i = (bcm->volt_c_r[0] << 16) | bcm->volt_c_r[1];
	bcm->attr[OBIX_BCM_ATTR_VOLTC] = u.f;
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_VOLTC]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_VOLTC]) > 0))
		bcm->attr[OBIX_BCM_ATTR_VOLTC] = mg->volt_l2n_def;

	u.i = (bcm->total_kwh_r[0] << 16) | bcm->total_kw_r[1];
	bcm->attr[OBIX_BCM_ATTR_TOTALKWH] = u.f;

	u.i = (bcm->total_kw_r[0] << 16) | bcm->total_kw_r[1];
	bcm->attr[OBIX_BCM_ATTR_TOTALKW] = u.f;

	u.i = (bcm->ac_avg_r[0] << 16) | bcm->ac_avg_r[1];
	bcm->attr[OBIX_BCM_ATTR_ACAVG] = u.f;

	return 0;
}

/*
 * Read BM register tables on each panel of the given BCM,
 * and convert big-endian raw data into floats for each BM.
 *
 * Return 0 on success, < 0 otherwise.
 */
static int mg_collect_bm(mg_bcm_t *bcm)
{
	mg_bm_t *bm;
	mg_modbus_t *bus = bcm->p;
	modbus_t *ctx = bus->ctx;
	obix_mg_t *mg = bus->p;
	int rc;
	int i, j;
	i2f_t u;
	float total_kw = FLT_EPSILON, total_ac = FLT_EPSILON;
	int do_total_kw = 0;

	/*
	 * Accumulate KW of all BMs on one BCM altogether, in case
	 * bcm->attr[OBIX_BCM_ATTR_TOTALKW] is invalid.
	 */
	if ((fabsf(bcm->attr[OBIX_BCM_ATTR_TOTALKW]) < FLT_EPSILON) ||
		(isnan(bcm->attr[OBIX_BCM_ATTR_TOTALKW]) > 0)) {
		do_total_kw = 1;
	}

	for (i = 0; i < MG_PANELS_PER_BCM; i++) {
		if (i > 0) {
			errno = 0;
			if (modbus_set_slave(ctx, bcm->slave_id + i) < 0) {
				log_error("Failed to set BCM %s slave_id %d to modbus ctx: %s",
							bcm->name, bcm->slave_id, modbus_strerror(errno));
				return -1;
			}
		}

		rc = mg_read_registers(bcm, mg->kwh.address, mg->kwh.count, bcm->kwh_r);
		if (rc < 0)
			return -1;

		rc = mg_read_registers(bcm, mg->kw.address, mg->kw.count, bcm->kw_r);
		if (rc < 0)
			return -1;

		rc = mg_read_registers(bcm, mg->pf.address, mg->pf.count, bcm->pf_r);
		if (rc < 0)
			return -1;

		rc = mg_read_registers(bcm, mg->I.address, mg->I.count, bcm->I_r);
		if (rc < 0)
			return -1;

		j = 0;
		list_for_each_entry(bm, &bcm->devices[i], list) {
			u.i = (bcm->I_r[2*j] << 16) | bcm->I_r[2*j+1];
			bm->attr[OBIX_BM_ATTR_I] = u.f;

			/*
			 * The summary of current intensity for all CB devices are
			 * calculated regardless of whether bcm->attr[OBIX_BCM_ATTR_ACAVG]
			 * is valid or not so as to provide a comparison between the
			 * amount of energy taken by a BCM device and that of output
			 * to racks through all its CB devices.
			 */
			total_ac += bm->attr[OBIX_BM_ATTR_I];

			switch(j % 3) {
				case 0:	bm->attr[OBIX_BM_ATTR_V] = bcm->attr[OBIX_BCM_ATTR_VOLTA];
						break;
				case 1:	bm->attr[OBIX_BM_ATTR_V] = bcm->attr[OBIX_BCM_ATTR_VOLTB];
						break;
				case 2:	bm->attr[OBIX_BM_ATTR_V] = bcm->attr[OBIX_BCM_ATTR_VOLTC];
						break;
				default:
						break;
			}

			u.i = bcm->pf_r[j];
			bm->attr[OBIX_BM_ATTR_PF] = u.f * (10 ^ MG_PF_SCALE_DEF);
			if ((fabsf(bm->attr[OBIX_BM_ATTR_PF]) < FLT_EPSILON) ||
				(isnan(bm->attr[OBIX_BM_ATTR_PF]) > 0))
				bm->attr[OBIX_BM_ATTR_PF] = mg->pf_def;

			u.i = (bcm->kw_r[2*j] << 16) | bcm->kw_r[2*j+1];
			bm->attr[OBIX_BM_ATTR_KW] = u.f;

			/*
			 * Manually caculate kilo watt value if relevant holding registers
			 * don't have a valid number. The result needs to be divided by
			 * 1000 since the unit of measurement is _kilo_ watt.
			 */
			if ((fabsf(bm->attr[OBIX_BM_ATTR_KW]) < FLT_EPSILON) ||
				(isnan(bm->attr[OBIX_BM_ATTR_KW]) > 0)) {
				bm->attr[OBIX_BM_ATTR_KW] = bm->attr[OBIX_BM_ATTR_I] *
											bm->attr[OBIX_BM_ATTR_V] *
											bm->attr[OBIX_BM_ATTR_PF] / 1000;
			}

			if (do_total_kw == 1) {
				total_kw += bm->attr[OBIX_BM_ATTR_KW];
			}

			u.i = (bcm->kwh_r[2*j] << 16) | bcm->kwh_r[2*j+1];
			bm->attr[OBIX_BM_ATTR_KWH] = u.f;

			j++;
		}
	}

	if (do_total_kw == 1) {
		bcm->attr[OBIX_BCM_ATTR_TOTALKW] = total_kw;
	}

	bcm->attr[OBIX_BCM_ATTR_TOTALAC] = total_ac;

	return 0;
}

/*
 * Refresh hardware status of one specific BCM, including AUX
 * and all BM on each of its panel.
 */
static void mg_collector_task_helper(mg_bcm_t *bcm)
{
	obix_mg_t *mg = mg_get_mg_bcm(bcm);
	int timeout = 0;

	while (mg_collect_aux(bcm) < 0) {
		/*
		 * If the current BCM is believed to have been off-lined,
		 * then just re-try once
		 */
		if (bcm->off_line == 1) {
			return;
		}

		bcm->timeout++;
		if (timeout++ < mg->collector_max_timeout) {
			sleep(mg->collector_sleep);
		} else {
			break;
		}
	}

	if (timeout == mg->collector_max_timeout) {
		log_warning("Failed to read AUX register table, perhaps BCM %s "
					"has been unplugged? Mark it as off-line", bcm->name);
		bcm->off_line = 1;
		return;
	}

	timeout = 0;
	while (mg_collect_bm(bcm) < 0) {
		bcm->timeout++;
		if (timeout++ < mg->collector_max_timeout) {
			sleep(mg->collector_sleep);
		} else {
			break;
		}
	}

	if (timeout == mg->collector_max_timeout) {
		log_warning("Failed to read AUX register table, perhaps BCM %s "
					"has been unplugged? Mark it as off-line", bcm->name);
		bcm->off_line = 1;
		return;
	}

	/*
	 * Successfully read from the device, mark it on-line again so that
	 * obix_updater could resume posting it status to oBIX server.
	 */
	if (bcm->off_line == 1) {
		bcm->off_line = 0;
	}

	if ((bcm->mtime = time(NULL)) < 0) {
		log_warning("Failed to get current time_t value");
		return;
	}

	/*
	 * Release existing timestamp string before allocating new one
	 */
	if (bcm->mtime_ts) {
		free(bcm->mtime_ts);
	}

	if (!(bcm->mtime_ts = get_utc_timestamp(bcm->mtime))) {
		log_warning("Failed to convert mtime into timestamp");
	}
}

/*
 * Refresh status of all BCM on a specific modbus line
 */
static void mg_collector_task(void *arg)
{
	mg_modbus_t *bus = (mg_modbus_t *)arg;
	mg_bcm_t *bcm;

	list_for_each_entry(bcm, &bus->devices, list) {
		pthread_mutex_lock(&bcm->mutex);

		/*
		 * Try to access the current BCM, regardless of whether it has
		 * been marked as off-line or not, so that it states could be
		 * updated and synchronized with oBIX server as soon as it has
		 * been re-connected.
		 */

		while (bcm->being_read == 1)	/* obix_updater is working on this dev */
			pthread_cond_wait(&bcm->wq, &bcm->mutex);

		bcm->being_written = 1;
		pthread_mutex_unlock(&bcm->mutex);

		mg_collector_task_helper(bcm);

		pthread_mutex_lock(&bcm->mutex);
		bcm->being_written = 0;
		pthread_cond_signal(&bcm->rq);
		pthread_mutex_unlock(&bcm->mutex);
	}
}

static int obix_update_aux(mg_bcm_t *bcm)
{
	mg_modbus_t *bus = bcm->p;
	Batch *batch;
	char buf[FLOAT_MAX_BITS + 1];
	int i;
	int error = OBIX_SUCCESS;

	if (!(batch = obix_batch_create(OBIX_CONNECTION_ID))) {
		return OBIX_ERR_NO_MEMORY;
	}

	/* Add batch commands for float attributes */
	for (i = 0; i < OBIX_BCM_ATTR_MAX; i++) {
		sprintf(buf, "%f", bcm->attr[i]);
		error = obix_batch_write_value(batch, bcm->history_name,
									   mg_bcm_attr[i], buf, OBIX_T_REAL);
		if (error < 0) {
			log_error("Failed to append batch command for %s attr of BCM %s",
						mg_bcm_attr[i], bcm->name);
			goto failed;
		}
	}

	/* Add batch command for the last modified timestamp */
	error = obix_batch_write_value(batch, bcm->history_name, MG_BCM_MTIME,
					(bcm->mtime_ts != NULL) ? bcm->mtime_ts : HIST_TS_INIT,
					OBIX_T_ABSTIME);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_MTIME, bcm->name);
		goto failed;
	}

	/* Add batch command for the off-line indicator */
	error = obix_batch_write_value(batch, bcm->history_name, MG_BCM_ONLINE,
					(bcm->off_line == 0) ? XML_TRUE : XML_FALSE,
					OBIX_T_BOOL);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_ONLINE, bcm->name);
		goto failed;
	}

	error = obix_batch_send(bus->handle, batch);
	if (error != OBIX_SUCCESS) {
		log_error("Failed to update %s via oBIX batch", bcm->name);
	}

	/* Fall through */

failed:
	obix_batch_destroy(batch);
	return error;
}

static int obix_update_aux_static(mg_bcm_t *bcm)
{
	mg_modbus_t *bus = bcm->p;
	Batch *batch;
	char buf[MG_ID_MAX_BITS + 2 + 1];
	int error = OBIX_SUCCESS;

	if (!(batch = obix_batch_create(OBIX_CONNECTION_ID))) {
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(buf, "0x%x", bcm->sn);
	error = obix_batch_write_value(batch, bcm->history_name,
								   MG_BCM_SN, buf, OBIX_T_INT);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_SN, bcm->name);
		goto failed;
	}

	sprintf(buf, "0x%x", bcm->firmware);
	error = obix_batch_write_value(batch, bcm->history_name,
								   MG_BCM_FW, buf, OBIX_T_INT);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_FW, bcm->name);
		goto failed;
	}

	sprintf(buf, "0x%x", bcm->model);
	error = obix_batch_write_value(batch, bcm->history_name,
								   MG_BCM_MOD, buf, OBIX_T_INT);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_MOD, bcm->name);
		goto failed;
	}

	sprintf(buf, "0x%x", bcm->ct_config);
	error = obix_batch_write_value(batch, bcm->history_name,
								   MG_BCM_CTC, buf, OBIX_T_INT);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_CTC, bcm->name);
		goto failed;
	}

	error = obix_batch_write_value(batch, bcm->history_name,
								   MG_BCM_LOC, bcm->location_r, OBIX_T_STR);
	if (error < 0) {
		log_error("Failed to append batch command for %s attr of BCM %s",
					MG_BCM_LOC, bcm->name);
		goto failed;
	}

	error = obix_batch_send(bus->handle, batch);
	if (error != OBIX_SUCCESS) {
		log_error("Failed to update %s via oBIX batch", bcm->name);
	}

	/* Fall through */

failed:
	obix_batch_destroy(batch);
	return error;
}

/*
 * Update the BM status on oBIX server via a batch object to
 * trim down network overhead.
 */
static int obix_update_bm_contract(mg_bm_t *bm)
{
	mg_bcm_t *bcm = bm->p;
	mg_modbus_t *bus = bcm->p;
	Batch *batch;
	char buf[FLOAT_MAX_BITS + 1];
	int j;
	int error = OBIX_SUCCESS;

	if (!(batch = obix_batch_create(OBIX_CONNECTION_ID))) {
		return OBIX_ERR_NO_MEMORY;
	}

	for (j = 0; j < OBIX_BM_ATTR_MAX; j++) {
		sprintf(buf, "%f", bm->attr[j]);
		error = obix_batch_write_value(batch, bm->history_name,
									   mg_bm_attr[j], buf, OBIX_T_REAL);
		if (error < 0) {
			log_error("Failed to append batch command for %s attr "
					  "of BM %s", mg_bm_attr[j], bm->name);
			goto failed;
		}
	}

	if ((error = obix_batch_send(bus->handle, batch)) != OBIX_SUCCESS) {
		log_error("Failed to update %s via oBIX batch", bm->name);
	}

	/* Fall through */

failed:
	obix_batch_destroy(batch);
	return error;
}

static int obix_append_bm_hist(mg_bm_t *bm)
{
	mg_bcm_t *bcm = bm->p;
	mg_modbus_t *bus = bcm->p;
	int error = OBIX_SUCCESS;

	/*
	 * History records for each BM on two panels of one BCM will
	 * share the same timestamp of when they are updated
	 *
	 * NOTE: since the timestamps in history records are when they
	 * are read from modbus controller which are unpredictable, the
	 * difference among them in consecutive history records does not
	 * necessarily equal to the fixed interval of history_period
	 */
	error= obix_create_history_ain(&bus->hist_ain, bcm->mtime_ts,
								   OBIX_BM_ATTR_MAX, mg_bm_attr, bm->attr);
	if (error != OBIX_SUCCESS) {
		log_error("Failed to create HistoryAppendIn contract for %s", bm->name);
		return error;
	}

	error = obix_append_history(bus->handle, OBIX_CONNECTION_ID,
								bm->history_name, bus->hist_ain);
	if (error != OBIX_SUCCESS) {
		log_error("Failed to append history record for %s", bm->name);
	}

	return error;
}

static int obix_update_bm(mg_bcm_t *bcm)
{
	obix_mg_t *mg = mg_get_mg_bcm(bcm);
	mg_bm_t *bm;
	int i;
	int error = OBIX_SUCCESS;

	for (i = 0; i < MG_PANELS_PER_BCM; i++) {
		list_for_each_entry(bm, &bcm->devices[i], list) {
			if ((error = obix_update_bm_contract(bm)) != OBIX_SUCCESS) {
				return error;
			}

			if (bcm->mtime < bm->htime) {
				/*
				 * No need to append a history record for current BM yet,
				 * continue to update the rest of CBs on the same panel
				 */
				continue;
			}

			bm->htime += mg->history_period;

			if ((error = obix_append_bm_hist(bm)) != OBIX_SUCCESS) {
				return error;
			}
		}
	}

	return OBIX_SUCCESS;
}

static void obix_updater_task_helper(mg_bcm_t *bcm)
{
	if (bcm->mtime == 0 || bcm->mtime < bcm->rtime) {
		bcm->slow_collector++;

		/*
		 * Current device not refreshed in a timely manner during
		 * the last interval of updater_period, therefore no need
		 * to re-post oBIX server with existing data that are
		 * already there
		 */
		return;
	}

	bcm->rtime = time(NULL);

	if (obix_update_aux(bcm) != OBIX_SUCCESS) {
		log_error("Failed to update AUX status on %s", bcm->name);
	}

	if (obix_update_bm(bcm) != OBIX_SUCCESS) {
		log_error("Failed to update BM status on %s", bcm->name);
	}
}

/*
 * Post status of all BCM on a specific modbus line to oBIX server
 * and append one history record for every single BM on each BCM
 */
static void obix_updater_task(void *arg)
{
	mg_modbus_t *bus = (mg_modbus_t *)arg;
	mg_bcm_t *bcm;

	list_for_each_entry(bcm, &bus->devices, list) {
		pthread_mutex_lock(&bcm->mutex);
		while (bcm->being_written == 1)		/* mg_collector is working on this dev */
			pthread_cond_wait(&bcm->rq, &bcm->mutex);

		if (bcm->off_line == 1) {
			/*
			 * Skip current device if it has been marked as off-lined
			 */
			pthread_mutex_unlock(&bcm->mutex);
			continue;
		}

		bcm->being_read = 1;
		pthread_mutex_unlock(&bcm->mutex);

		obix_updater_task_helper(bcm);

		pthread_mutex_lock(&bcm->mutex);
		bcm->being_read = 0;
		pthread_cond_signal(&bcm->wq);
		pthread_mutex_unlock(&bcm->mutex);
	}
}

/*
 * If a BCM is off-lined at adapter program's start-up, its static
 * information won't have a chance to be fetched from relevant device
 * even if a mg_collector thread has found it on-lined, since that
 * thread will only care about reading dynamic information from the
 * device.
 *
 * To this end, system adaministrator could still use this function
 * and send SIGUSR1 signal after re-connects it, as in the following
 * command:
 *
 *		kill -USR1 -q <slave_id> <pid>
 *
 * where pid is the process ID of this mg_adater program and slave_id
 * is the slave ID of the BCM just brought on-lined.
 */
static void mg_resurrect_dev(obix_mg_t *mg, int slave_id)
{
	mg_modbus_t *bus;
	mg_bcm_t *bcm;

	log_debug("%d is brought alive", slave_id);

	list_for_each_entry(bus, &mg->devices, list) {
		list_for_each_entry(bcm, &bus->devices, list) {
			if (bcm->slave_id != slave_id) {
				continue;
			}

			pthread_mutex_lock(&bcm->mutex);
			while (bcm->being_written == 1 || bcm->being_read == 1) {
				pthread_mutex_unlock(&bcm->mutex);
				sleep(1);
				pthread_mutex_lock(&bcm->mutex);
			}

			/*
			 * With bcm->mutex held, no consumer nor producer thread
			 * would race against us
			 */
			if (mg_collect_aux_static(bcm) < 0) {
				log_error("Still failed to read from %s", bcm->name);
				pthread_mutex_unlock(&bcm->mutex);
				return;
			}

			if (bcm->off_line == 1) {
				bcm->off_line = 0;
			}

			if (obix_update_aux_static(bcm) != OBIX_SUCCESS) {
				log_error("Failed to update AUX status on %s", bcm->name);
			}

			pthread_mutex_unlock(&bcm->mutex);
		}
	}
}

static void mg_signal_handler_exit(int signo)
{
	if (signo == SIGINT) {
		flag_exiting = 1;
	}
}

static void mg_signal_handler_user(int signo, siginfo_t *si, void *ucontext)
{
	if (signo == SIGUSR1) {
		flag_online_id = si->si_int;
	}
}

int main(int argc, char *argv[])
{
	obix_mg_t *mg;
	struct sigaction sa;
	int ret = OBIX_SUCCESS;

	if (argc != 3) {
		printf("Usage: %s <device_config_file> <obix_config_file>\n", argv[0]);
		return -1;
	}

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = mg_signal_handler_exit;
	if (sigaction(SIGINT, &sa, NULL) < 0) {
		log_error("Failed to register SIGINT handler");
		return -1;
	}

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = mg_signal_handler_user;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		log_error("Failed to register SIGUSR1 handler");
		return -1;
	}

	xml_parser_init();

	if (!(mg = mg_setup_mg(argv[1]))) {
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto setup_failed;
	}

	if ((ret = obix_setup_connections(argv[2])) != OBIX_SUCCESS) {
		goto conns_failed;
	}

	if ((ret = obix_open_connection(OBIX_CONNECTION_ID)) != OBIX_SUCCESS) {
		goto open_failed;
	}

	if ((ret = mg_register_devices(mg)) != OBIX_SUCCESS) {
		goto register_failed;
	}

	if ((ret = mg_schedule_tasks(mg)) != OBIX_SUCCESS) {
		goto tasks_failed;
	}

	/*
	 * Suspend until waken up by signals sent from command line, then
	 * examine the flags and then invoke handlers accordingly.
	 *
	 * In theory these flags are critical regions between the main thread
	 * and asynchronous signal handlers, therefore signals should be blocked
	 * before accessed by the main thread so as to prevent race conditions
	 * between the two.
	 *
	 * However, signals are sent from command line by human users, so we
	 * can safely assume that no signals would arrive in the short notice
	 * right after the main thread gets waken up by a previous signal
	 * instance.
	 */
	while (1) {
		pause();

		if (flag_exiting == 1) {
			break;
		}

		if (flag_online_id > 0) {
			mg_resurrect_dev(mg, flag_online_id);
			flag_online_id = 0;
		}
	}

	mg_cancel_tasks(mg);

	/* Fall through */

tasks_failed:
	mg_unregister_devices(mg);

register_failed:
	obix_destroy_connection(OBIX_CONNECTION_ID);

open_failed:
	obix_destroy_connections();

conns_failed:
	mg_destroy_mg(mg);

setup_failed:
	xml_parser_exit();

	return ret;
}
