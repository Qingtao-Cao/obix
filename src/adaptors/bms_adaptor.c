/* *****************************************************************************
 * Copyright (c) 2014 Qingtao Cao [harry.cao@nextdc.com]
 *
 * This file is part of obix-adaptors.
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
 * along with obix-adaptors. If not, see <http://www.gnu.org/licenses/>.
 *
 * *****************************************************************************/

#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/inotify.h>
#include <unistd.h>
#include "obix_client.h"
#include "log_utils.h"
#include "ptask.h"
#include "csv_ext.h"
#include "xml_utils.h"
#include "xml_config.h"

/*
 * Enable this macro to debug CSV callbacks
 */
#undef DEBUG_CSV

/* XPath predicates used when parsing the configuration files */
static const char *XP_BMS_ID = "/config/meta/bms_id";
static const char *XP_UPDATER_PERIOD = "/config/meta/updater_period";
static const char *XP_PARENT_HREF = "/config/meta/parent_href";
static const char *XP_HISTORY_ROOT = "/config/meta/history_root";
static const char *XP_CSV_DIR = "/config/meta/csv_dir";
static const char *XP_CSV_PREFIX = "/config/meta/csv_prefix";
static const char *XP_CSV_SUFFIX = "/config/meta/csv_suffix";
static const char *XP_CSV_NEWDIR = "/config/meta/csv_newdir";
static const char *XP_BTANKS = "/config/bulk_tanks/obj";
static const char *XP_DTANKS = "/config/day_tanks/obj";

typedef enum {
	BMS_SB_LIST_HVSB = 0,
	BMS_SB_LIST_MSB = 1,
	BMS_SB_LIST_MAX = 2
} BMS_SB_LIST;

static const char *xp_sbs[] = {
	[BMS_SB_LIST_HVSB] = "/config/hv_switchboards/obj",
	[BMS_SB_LIST_MSB] = "/config/main_switchboards/obj"
};

/* XPath predicates used when manipulating history templates */
static const char *XP_HIST_SB = "/history/obj[@name='sb']";
static const char *XP_HIST_SB_IFDRS = "/history/obj[@name='sb']/list[@name='data']/obj[@is='obix:HistoryRecord']/list[@name='input_feeders']";
static const char *XP_HIST_SB_OFDRS = "/history/obj[@name='sb']/list[@name='data']/obj[@is='obix:HistoryRecord']/list[@name='output_feeders']";
static const char *XP_HIST_SB_TS = "/history/obj[@name='sb']/list[@name='data']/obj[@is='obix:HistoryRecord']/abstime[@name='timestamp']";

static const char *XP_HIST_BMS = "/history/obj[@name='bms']";
static const char *XP_HIST_BMS_BTANKS = "/history/obj[@name='bms']/list[@name='data']/obj[@is='obix:HistoryRecord']/list[@name='bulk_tanks']";
static const char *XP_HIST_BMS_DTANKS = "/history/obj[@name='bms']/list[@name='data']/obj[@is='obix:HistoryRecord']/list[@name='day_tanks']";
static const char *XP_HIST_BMS_TS = "/history/obj[@name='bms']/list[@name='data']/obj[@is='obix:HistoryRecord']/abstime[@name='timestamp']";

static const char *XP_HIST_FDR = "/history/obj[@name='fdr']";
static const char *XP_HIST_BTANK = "/history/obj[@name='bulk_tank']";
static const char *XP_HIST_DTANK = "/history/obj[@name='day_tank']";

/*
 * Strings used as tags in the device configuration file, or
 * hrefs in device contracts
 */
static const char *BTANKS = "bulk_tanks";
static const char *DTANKS = "day_tanks";
static const char *KWH = "kWh";
static const char *KW = "kW";
static const char *LEVEL = "level";

static const char *OBIX_ATTR_TYPE = "type";

/*
 * The desirable inotify events to receive from kernel.
 * Following events will be sent from kernel in the suggested
 * order upon the creation of a new file under the watched
 * folder:
 *
 *	IN_CREATE > IN_OPEN > IN_MODIFY > IN_CLOSE_WRITE
 *
 * NOTE: the IN_CREATE event is sent as soon as the new file
 * is just created but NOT written into yet, therefore the
 * reader would have to wait for the IN_CLOSE_WRITE event
 *
 * NOTE: no need to wait for the IN_OPEN events because they
 * are also to do with reading from existing files.
 */
static const int BMS_INOTIFY_MASK = (IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE);

/*
 * Practically one inotify event takes up to 64 bytes, and BMS
 * generates only one CSV file each time
 */
static const int BMS_INOTIFY_BUFLEN = 128;

/* The options for the CSV Parser */
#define BMS_CSV_OPTS	(CSV_APPEND_NULL)

/* The delimiter adopted in current BMS's exported file */
static const unsigned char BMS_CSV_DELIM = CSV_TAB;

/* The index of various fields in a BMS CSV record */
#define BMS_CSV_KEY_IDX		1
#define BMS_CSV_VAL_IDX		2

/* oBIX contracts for various devices
 *
 * NOTE: Contracts such as feeders, bulk tanks and day tanks that are
 * not registered standalone, they are added directly to relevant
 * lists of their parent contracts. To this end, the placeholder for
 * the list node in their parent contract must be included.
 *
 * NOTE: obix_write() expects a well-formed XML file therefore a XML
 * header is appended for each contract
 */
static const char *SB_FDR_CONTRACT =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<list href=\"%s\">\r\n"
"<obj name=\"%s\" href=\"%s\" is=\"nextdc:power_meter\">\r\n"
"<real name=\"kW\" href=\"kW\" val=\"%.1f\" writable=\"true\"/>\r\n"
"<real name=\"kWh\" href=\"kWh\" val=\"%.1f\" writable=\"true\"/>\r\n"
"</obj>\r\n"
"</list>\r\n";

static const char *BMS_BTANK_CONTRACT =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<list href=\"%s\">\r\n"
"<obj name=\"%s\" href=\"%s\" is=\"nextdc:bulk_tank\">\r\n"
"<int name=\"level\" href=\"level\" val=\"%d\" writable=\"true\"/>\r\n"
"</obj>\r\n"
"</list>\r\n";

static const char *BMS_DTANK_CONTRACT =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
"<list href=\"%s\">\r\n"
"<obj name=\"%s\" href=\"%s\" is=\"nextdc:day_tank\">\r\n"
"<str name=\"lvl_10\" href=\"lvl_10\" val=\"%s\" writable=\"true\"/>\r\n"
"<str name=\"lvl_25\" href=\"lvl_25\" val=\"%s\" writable=\"true\"/>\r\n"
"<str name=\"lvl_50\" href=\"lvl_50\" val=\"%s\" writable=\"true\"/>\r\n"
"<str name=\"lvl_98\" href=\"lvl_98\" val=\"%s\" writable=\"true\"/>\r\n"
"</obj>\r\n"
"</list>\r\n";

/*
 * Input or output feeders on HVSB always have 2 attributes
 * of kW and kWh
 */
static const int HVSB_FDR_ATTRIB_MIN = 2;

/*
 * A MSB input feeder has 5 attributes, while a output feeder
 * has 2 or 3 attributes
 */
static const int MSB_FDR_ATTRIB_MIN = 2;

/*
 * Float data in CSV files only have one digit in the decimal part
 */
static const char *FORMAT_FLOAT = "%.1f";
static const char *FORMAT_INT = "%d";

typedef enum {
	DTANK_LVL_10 = 0,
	DTANK_LVL_25 = 1,
	DTANK_LVL_50 = 2,
	DTANK_LVL_98 = 3,
	DTANK_LVL_MAX = 4
} DTANK_LVL;

static const char *dtank_lvl[] = {
	[DTANK_LVL_10] = "lvl_10",
	[DTANK_LVL_25] = "lvl_25",
	[DTANK_LVL_50] = "lvl_50",
	[DTANK_LVL_98] = "lvl_98",
};

static const char *BMS_SB_CONTRACT =
"<obj name=\"%s\" href=\"%s\" is=\"nextdc:switchboard\">\r\n"
"<list name=\"input_feeders\" href=\"input_feeders\" is=\"obix:list\" of=\"obix:obj nextdc:power_meter\" writable=\"true\"/>\r\n"
"<list name=\"output_feeders\" href=\"output_feeders\" is=\"obix:list\" of=\"obix:obj nextdc:power_meter\" writable=\"true\"/>\r\n"
"</obj>\r\n";

/*
 * Highlight the latest modification timestamp as the first
 * subnode of the contract
 */
static const char *BMS_MESN_CONTRACT =
"<obj name=\"%s\" href=\"%s\" is=\"nextdc:mesn\">\r\n"
"<abstime name=\"last_updated\" href=\"last_updated\" val=\"%s\" writable=\"true\"/>\r\n"
"<list name=\"hv_switchboards\" href=\"hv_switchboards\" is=\"obix:list\" of=\"obix:obj nextdc:switchboard\" writable=\"true\"/>\r\n"
"<list name=\"main_switchboards\" href=\"main_switchboards\" is=\"obix:list\" of=\"obix:obj nextdc:switchboard\" writable=\"true\"/>\r\n"
"<list name=\"bulk_tanks\" href=\"bulk_tanks\" is=\"obix:list\" of=\"obix:obj nextdc:bulk_tank\" writable=\"true\"/>\r\n"
"<list name=\"day_tanks\" href=\"day_tanks\" is=\"obix:list\" of=\"obix:obj nextdc:day_tank\" writable=\"true\"/>\r\n"
"</obj>\r\n";

static const char *BMS_MTIME = "last_updated";

/* Values of the level indicators of Day Tanks */
typedef enum {
	LVL_OFF = 0,		/* The default value should be "Off" */
	LVL_ON = 1
} LVL_MTR;

static const char *lvl_mtr[] = {
	[LVL_OFF] = "Off",
	[LVL_ON] = "On"
};

static const int DTANK_LVL_MAX_BITS = 3;

/*
 * Descriptor for a value and its type read from BMS CSV file
 *
 * The device configuration file should explicitly specify the
 * type of the value since relevant Type settings in the CSV file
 * are not trustworthy
 */
typedef enum {
	MTR_TYPE_FLOAT = 0,
	MTR_TYPE_UINT16 = 1,
	MTR_TYPE_UINT32 = 2,
	MTR_TYPE_BOOL = 3,
	MTR_TYPE_MAX = 4
} MTR_TYPE;

static const char *mtr_type[] = {
	[MTR_TYPE_FLOAT] = "float",
	[MTR_TYPE_UINT16] = "uint16",
	[MTR_TYPE_UINT32] = "uint32",
	[MTR_TYPE_BOOL] = "bool"
};

typedef struct bms_mtr {
	/* Key or name of relevant CSV record */
	unsigned char *key;

	/* Value of the record */
	union {
		float f;
		uint16_t u16;
		uint32_t u32;
		LVL_MTR b;
	} value;

	/* Type of the value read */
	MTR_TYPE type;
} bms_mtr_t;

static const uint16_t UINT16_MASK = (uint16_t)-1;		/* ffff */
static const uint32_t UINT32_MASK = (uint32_t)-1;		/* ffffffff */

/*
 * Descriptor for an input or output feeder on a high voltage
 * switch board
 */
typedef struct hvsb_fdr {
	char *name;
	bms_mtr_t kW;
	bms_mtr_t kWh;
	struct list_head list;
} hvsb_fdr_t;

/*
 * Descriptor for the input feeder of a main switch board
 */
typedef enum {
	MSB_FDR_KWH_R1 = 0,
	MSB_FDR_KWH_R2 = 1,
	MSB_FDR_KWH_R3 = 2,
	MSB_FDR_KWH_R4 = 3,
	MSB_FDR_KWH_MAX = 4,
} MSB_FDR_KWH;

/*
 * The modulus of kWh readings on MSB feeders
 */
static const uint16_t MSB_FDR_KWH_MODULUS = 10000;

static const char *msb_fdr_kwh[] = {
	[MSB_FDR_KWH_R1] = "kWhR1",
	[MSB_FDR_KWH_R2] = "kWhR2",
	[MSB_FDR_KWH_R3] = "kWhR3",
	[MSB_FDR_KWH_R4] = "kWhR4",
};

/*
 * Descriptor for the input and output feeders on a MSB,
 * although each input feeder (or, ACB) always has 4 kWh
 * readings while output feeders may have 1 or 2 kWh
 * readings.
 *
 * NOTE: the first kWh reading (or kWhR1) will be treated
 * as a float so as to carry a decimal part, while the rest
 * of possible readings are regarded as uint16_t.
 *
 * If there is less number of kWh readings in the CSV file,
 * then relevant bms_mtr_t simply carries zero value
 */
typedef struct msb_fdr {
	char *name;
	bms_mtr_t kW;
	bms_mtr_t kWh[MSB_FDR_KWH_MAX];
	struct list_head list;
} msb_fdr_t;

/*
 * Descriptor for a Bulk Tank which has one fuel level sensor
 */
typedef struct bms_btank {
	char *name;
	bms_mtr_t level;
	struct list_head list;
} bms_btank_t;

/*
 * Descriptor for a Day Tank which has a number of level indicator:
 * "On" if the current fuel level is above relevant indicator,
 * "Off" otherwise
 */
typedef struct bms_dtank {
	char *name;
	bms_mtr_t levels[DTANK_LVL_MAX];
	struct list_head list;
} bms_dtank_t;

/*
 * The index for the input and output feeder lists
 * on a HVSB or MSB
 */
typedef enum {
	SB_FDR_LIST_IN = 0,
	SB_FDR_LIST_OUT = 1,
	SB_FDR_LIST_MAX = 2
} SB_FDR_LIST;

static const char *sb_fdr_list[] = {
	[SB_FDR_LIST_IN] = "input_feeders",
	[SB_FDR_LIST_OUT] = "output_feeders"
};

static const char *bms_sb_list[] = {
	[BMS_SB_LIST_HVSB] = "hv_switchboards",
	[BMS_SB_LIST_MSB] = "main_switchboards"
};

struct bms_sb;

typedef int (*fdr_cb_t)(struct bms_sb *sb, const char *parent_list, const char *name,
						const float kw, const float kwh, void *arg);
/*
 * Descriptor for the high voltage switch board and/or
 * the main switch board, the difference between them
 * lie in the type of feeder descriptors in the fdrs
 * lists
 */
typedef struct bms_sb {
	/* The name of the device from config the file */
	char *name;

	/*
	 * The unique name of the current device's history facility,
	 * which does not necessarily have to be the same with the
	 * device's href. For example, there is no "hv_switchboards/"
	 * component in the history_name of a HVSB as in its href
	 */
	char *history_name;

	/* The href of the device's contract on oBIX server */
	char *href;

	/* The list of input and output feeders */
	struct list_head fdrs[SB_FDR_LIST_MAX];

	/* The type of current switchboard: HVSB or MSB */
	BMS_SB_LIST type;

	/* To join relevant obix_bms_t.sbs[sb->type] list */
	struct list_head list;

	/*
	 * The operations to setup and destroy a particular type of
	 * input/output feeder descriptors for a switch board
	 */
	int (*setup_fdr)(struct bms_sb *, const int, xmlNode *);
	void (*destroy_fdr)(void *);

	/*
	 * The operation to destroy a particular type of switch board
	 * descriptor
	 */
	void (*destroy_sb)(struct bms_sb *);

	/*
	 * The operation to traverse lists of feeder descriptors on
	 * a specific switch board and apply the given callback function
	 * on each of them
	 *
	 * So far the supported callbacks include registering feeders'
	 * contracts into that of the switch board and having them updated
	 */
	int (*for_each_fdr)(struct bms_sb *, fdr_cb_t, void *);

	/*
	 * The operation to setup the in-memory history template
	 * of a switch board, by populating it with XML nodes of
	 * all its onboard feeders
	 */
	int (*setup_hist)(xmlNode *, xmlNode *, xmlNode *, struct bms_sb *);
} bms_sb_t;


/* Descriptor for the BMS */
typedef struct obix_bms {
	/* The name of the device from the config file */
	char *name;

	/* The root of relevant history facility's HREF on the oBIX server */
	char *history_root;

	/* The href of the device's contract on oBIX server */
	char *href;

	/* The parent contract's HREF on the oBIX Server */
	char *parent_href;

	/* The unique name of the current device's history facility */
	char *history_name;

	/* The period of the bms_updater thread */
	int updater_period;

	/* The absolute path of the CSV files folder */
	char *csv_dir;

	/* The prefix and suffix for all valid CSV files */
	char *csv_prefix;
	char *csv_suffix;

	/* Where the processed CSV files should be moved to */
	char *csv_newdir;

	/* The mtime of a CSV file as in `/usr/bin/date +%FT%T` format */
	char *mtime_ts;

	/* The list of high voltage switch boards and main switch boards */
	struct list_head sbs[BMS_SB_LIST_MAX];

	/* The list of day tanks*/
	struct list_head dtanks;

	/* The list of bulk tanks */
	struct list_head btanks;

	/*
	 * The worker thread to read BMS exported CSV file and
	 * update relevant contracts on the oBIX server
	 */
	obix_task_t bms_updater;

	/*
	 * Inotify descriptor and the watch descriptor for the
	 * CSV folder
	 */
	int fd;
	int wd;

	/*
	 * The XML DOM tree providing various templates for the
	 * purpose of history record generation
	 */
	xml_config_t *history;
} obix_bms_t;

/* CSV files, parser and state machine */
csv_state_t *_csv;

static void bms_updater_task(void *);

#ifdef DEBUG_CSV
static void bms_debug_csv_records(csv_state_t *csv)
{
	int i = 0;
	csv_record_t *r;
	bms_mtr_t *mtr;

	list_for_each_entry(r, &csv->wanted, list) {
		mtr = (bms_mtr_t *)(r->data);
		log_debug("#%d: key: %s", i++, mtr->key);
	}
}

static void bms_debug_csv_cb1(const char *f, size_t len)
{
	/*
	 * Copy and append a null terminator regardless of
	 * whether the given string snippet is terminated
	 * with null or not
	 */
	char *buf;

	if (!(buf = (char *)malloc(len + 1))) {
		return;
	}

	snprintf(buf, len+1, "%s", f);

	log_debug("cb1: %s", buf);

	free(buf);
}
#endif

/*
 * The preprocessor of BMS exported data, in which an extra leading
 * byte of "0x00" are placed for every single character in ASCII
 * encoding format
 *
 * Therefore all these leading zero bytes must be removed before
 * libcsv can parse it properly
 *
 * Return the number of bytes remained after pre-processing.
 */
static size_t bms_csv_p(void *buf, size_t len)
{
	size_t i, w;
	char *data = (char *)buf;

	assert(buf && len > 0);

	for (i = 0, w = 0; i < len; i++) {
		if (data[i] != 0 && i > w) {
			data[w] = data[i];
			w++;
		}
	}

	return w;
}

/*
 * Implement the policy how to manipulate the CSV file, that is,
 * how the BMS Adaptor would like to use the data in that file.
 *
 * The name of each record in the CSV file is checked, if wanted,
 * its value is stored into the user-specific descriptor pointed
 * to by relevant csv_record_t descriptor.
 */
static void bms_csv_cb1(void *f, size_t len, void *arg)
{
	csv_state_t *state = (csv_state_t *)arg;
	char *field = (char *)f;
	csv_record_t *record;
	bms_mtr_t *meter;
	long val;
	int ret;

	/* NOTE: cb1 can be invoked with empty string! */
	if (len == 0) {
		return;
	}

#ifdef DEBUG_CSV
	bms_debug_csv_cb1(field, len);
#endif

	if (list_empty(&state->wanted) == 1) {
		log_warning("Not specified the wanted CSV records yet");
		return;
	}

	state->fields_count++;

	if (state->fields_count == BMS_CSV_KEY_IDX) {
		list_for_each_entry(record, &state->wanted, list) {
			meter = (bms_mtr_t *)(record->data);
			if (strncasecmp((char *)meter->key, field, len) == 0) {
#ifdef DEBUG_CSV
				log_debug("Matching record found, key %s", meter->key);
#endif
				state->matching = meter;
				return;
			}
		}
	} else {
		if ((meter = (bms_mtr_t *)(state->matching)) != NULL &&
			state->fields_count == BMS_CSV_VAL_IDX) {
			switch (meter->type) {
			case MTR_TYPE_FLOAT:
				if ((ret = str_to_float(field, &(meter->value).f)) < 0) {
					log_error("Failed to get float for %s out of %s: %d",
							  meter->key, field, ret);
				}
				break;
			case MTR_TYPE_UINT16:
				if ((ret = str_to_long(field, &val)) < 0) {
					log_error("Failed to get long for %s out of %s: %d",
							  meter->key, field, ret);
				} else {
					meter->value.u16 = val & UINT16_MASK;
				}
				break;
			case MTR_TYPE_UINT32:
				if ((ret = str_to_long(field, &val)) < 0) {
					log_error("Failed to get long for %s out of %s: %d",
							  meter->key, field, ret);
				} else {
					meter->value.u32 = val & UINT32_MASK;
				}
				break;
			case MTR_TYPE_BOOL:
				if (strcasecmp(field, lvl_mtr[LVL_ON]) == 0) {
					meter->value.b = LVL_ON;
				} else if (strcasecmp(field, lvl_mtr[LVL_OFF]) == 0) {
					meter->value.b = LVL_OFF;
				} else {
					log_error("Failed to get bool for %s out of %s",
							  meter->key, field);
				}
				break;
			default:
				log_error("Unknown record type: %d", meter->type);
				break;
			}
		}
	}
}

/*
 * Reset the internal state machine when the end of
 * a record is encountered
 */
static void bms_csv_cb2(int c, void *arg)
{
	csv_state_t *state = (csv_state_t *)arg;

	state->fields_count = 0;
	state->matching = NULL;
}

static csv_ops_t bms_csv_ops = {
	.p = bms_csv_p,
	.cb1 = bms_csv_cb1,
	.cb2 = bms_csv_cb2,
};

/* Flag shared among signal handler and the main thread */
static int flag_exit;

/* The signal captured for cleanup purpose */
static const int BMS_SIGNAL_CLEANUP = SIGINT;

static void bms_signal_handler(int signo)
{
	if (signo == BMS_SIGNAL_CLEANUP) {
		flag_exit = 1;
	}
}

/*
 * NOTE: The data type pointed to by val must be consistent
 * with the meter being read, otherwise *val can be truncated
 * unexpectedly!
 *
 * For example, a float pointer should NOT be passed in to
 * read a meter of uint16_t type.
 */
static void get_mtr_reading(bms_mtr_t *mtr, void *val)
{
	assert(val);

	switch (mtr->type) {
	case MTR_TYPE_FLOAT:
		*(float *)val += mtr->value.f;
		break;
	case MTR_TYPE_UINT16:
		*(uint16_t *)val += mtr->value.u16;
		break;
	case MTR_TYPE_UINT32:
		*(uint32_t *)val += mtr->value.u32;
		break;
	case MTR_TYPE_BOOL:
		*(LVL_MTR *)val += mtr->value.b;
		break;
	default:
		/*
		 * The current meter is not needed,
		 * untouch the value
		 */
		break;
	}
}

/*
 * Calculate the final kWh reading of a MSB feeder, which
 * is simply a sigma of all register readings with a modulus
 * of 10,000
 *
 * NOTE: The R1 reading is a float, while the rest registers
 * are all uint16_t, therefore they should be treated differently.
 */
static void get_msb_fdr_kwh(bms_mtr_t *mtr, const int max, float *val)
{
	int i;
	uint16_t u16;

	assert(val);

	for (i = max - 1; i >= 1; i--) {
		*val *= MSB_FDR_KWH_MODULUS;

		u16 = 0;
		get_mtr_reading(mtr+i, &u16);

		*val += u16;
	}

	*val *= MSB_FDR_KWH_MODULUS;
	get_mtr_reading(mtr, val);
}

static int for_each_msb_fdr(bms_sb_t *sb, fdr_cb_t cb, void *arg)
{
	float kw, kwh;
	int ret = OBIX_SUCCESS, i;
	msb_fdr_t *n;

	for (i = 0; i < SB_FDR_LIST_MAX; i++) {
		list_for_each_entry(n, &sb->fdrs[i], list) {
			kw = kwh = 0;
			get_mtr_reading(&n->kW, &kw);
			get_msb_fdr_kwh(n->kWh, MSB_FDR_KWH_MAX, &kwh);

			if ((ret = cb(sb, sb_fdr_list[i], n->name, kw, kwh,
						  arg)) != OBIX_SUCCESS) {
				return ret;
			}
		}
	}

	return ret;
}

static int for_each_hvsb_fdr(bms_sb_t *sb, fdr_cb_t cb, void *arg)
{
	float kw, kwh;
	int ret = OBIX_SUCCESS, i;
	hvsb_fdr_t *n;

	for (i = 0; i < SB_FDR_LIST_MAX; i++) {
		list_for_each_entry(n, &sb->fdrs[i], list) {
			kw = kwh = 0;
			get_mtr_reading(&n->kW, &kw);
			get_mtr_reading(&n->kWh, &kwh);

			if ((ret = cb(sb, sb_fdr_list[i], n->name, kw, kwh,
						  arg)) != OBIX_SUCCESS) {
				return ret;
			}
		}
	}

	return ret;
}

static void bms_destroy_hvsb_fdr(void *arg)
{
	hvsb_fdr_t *fdr = (hvsb_fdr_t *)arg;

	if (fdr->name) {
		free(fdr->name);
	}

	if (fdr->kW.key) {
		free(fdr->kW.key);
	}

	if (fdr->kWh.key) {
		free(fdr->kWh.key);
	}

	free(fdr);
}

static void bms_destroy_sb_core(bms_sb_t *sb)
{
	if (!sb) {
		return;
	}

	if (sb->name) {
		free(sb->name);
	}

	if (sb->history_name) {
		free(sb->history_name);
	}

	if (sb->href) {
		free(sb->href);
	}

	free(sb);
}

static void bms_destroy_hvsb(bms_sb_t *sb)
{
	hvsb_fdr_t *fdr, *n;
	int i;

	assert(sb);

	for (i = 0; i < SB_FDR_LIST_MAX; i++) {
		list_for_each_entry_safe(fdr, n, &sb->fdrs[i], list) {
			list_del(&fdr->list);
			sb->destroy_fdr(fdr);
		}
	}

	bms_destroy_sb_core(sb);
}

static void bms_destroy_sbs(obix_bms_t *bms, int which)
{
	bms_sb_t *sb, *n;

	assert(which == BMS_SB_LIST_HVSB || which == BMS_SB_LIST_MSB);

	list_for_each_entry_safe(sb, n, &bms->sbs[which], list) {
		list_del(&sb->list);
		sb->destroy_sb(sb);
	}
}

static void bms_destroy_msb_fdr(void *arg)
{
	msb_fdr_t *fdr = (msb_fdr_t *)arg;
	int i;

	if (fdr->name) {
		free(fdr->name);
	}

	if (fdr->kW.key) {
		free(fdr->kW.key);
	}

	for (i = 0; i < MSB_FDR_KWH_MAX; i++) {
		if (fdr->kWh[i].key) {
			free(fdr->kWh[i].key);
		}
	}

	free(fdr);
}

static void bms_destroy_msb(bms_sb_t *sb)
{
	msb_fdr_t *fdr, *n;
	int i;

	assert(sb);

	for (i = 0; i < SB_FDR_LIST_MAX; i++) {
		list_for_each_entry_safe(fdr, n, &sb->fdrs[i], list) {
			list_del(&fdr->list);
			sb->destroy_fdr(fdr);
		}
	}

	bms_destroy_sb_core(sb);
}

static void bms_destroy_btank(bms_btank_t *btank)
{
	if (btank->name) {
		free(btank->name);
	}

	if (btank->level.key) {
		free(btank->level.key);
	}

	free(btank);
}

static void bms_destroy_btanks(obix_bms_t *bms)
{
	bms_btank_t *btank, *n;

	list_for_each_entry_safe(btank, n, &bms->btanks, list) {
		list_del(&btank->list);
		bms_destroy_btank(btank);
	}
}

static void bms_destroy_dtank(bms_dtank_t *dtank)
{
	int i;

	if (dtank->name) {
		free(dtank->name);
	}

	for (i = 0; i < DTANK_LVL_MAX; i++) {
		if (dtank->levels[i].key) {
			free(dtank->levels[i].key);
		}
	}

	free(dtank);
}

static void bms_destroy_dtanks(obix_bms_t *bms)
{
	bms_dtank_t *dtank, *n;

	list_for_each_entry_safe(dtank, n, &bms->dtanks, list) {
		list_del(&dtank->list);
		bms_destroy_dtank(dtank);
	}
}

static void bms_destroy_param(obix_bms_t *bms)
{
	if (bms->name) {
		free(bms->name);
	}

	if (bms->history_name) {
		free(bms->history_name);
	}

	if (bms->href) {
		free(bms->href);
	}

	if (bms->parent_href) {
		free(bms->parent_href);
	}

	if (bms->history_root) {
		free(bms->history_root);
	}

	if (bms->csv_dir) {
		free(bms->csv_dir);
	}

	if (bms->csv_prefix) {
		free(bms->csv_prefix);
	}

	if (bms->csv_suffix) {
		free(bms->csv_suffix);
	}

	if (bms->csv_newdir) {
		free(bms->csv_newdir);
	}
}

static void bms_destroy_bms(obix_bms_t *bms)
{
	int i;

	if (!bms) {
		return;
	}

	obix_destroy_task(&bms->bms_updater);
	csv_destroy_csv(_csv);

	for (i = 0; i < BMS_SB_LIST_MAX; i++) {
		bms_destroy_sbs(bms, i);
	}

	bms_destroy_btanks(bms);
	bms_destroy_dtanks(bms);

	bms_destroy_param(bms);

	if (bms->mtime_ts) {
		free(bms->mtime_ts);
	}

	free(bms);
}

/*
 * Read a meter's CSV record's name and type from relevant
 * setting in the device configuration file
 *
 * Also create one csv_record_t to keep track of the current
 * meter
 */
static int bms_get_csv_settings(xmlNode *node, bms_mtr_t *mtr)
{
	char *name = NULL, *type = NULL;
	int i, ret = OBIX_ERR_INVALID_ARGUMENT;

	if (!(name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME)) ||
		!(type = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_TYPE))) {
		goto failed;
	}

	if (!(mtr->key = (unsigned char *)strdup(name))) {
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	for (i = 0; i < MTR_TYPE_MAX; i++) {
		if (strcmp(type, mtr_type[i]) == 0) {
			mtr->type = i;
			if ((ret = csv_add_record(_csv, mtr)) != OBIX_SUCCESS) {
				break;
			} else {
				free(name);
				free(type);
				return OBIX_SUCCESS;
			}
		}
	}

	/*
	 * Not found, illegal type setting
	 *
	 * NOTE: the added csv_record_t will be released before the
	 * main thread exits due to configuration error
	 */
	free(mtr->key);
	mtr->key = NULL;

failed:
	if (name) {
		free(name);
	}

	if (type) {
		free(type);
	}

	return ret;
}

static int bms_setup_hvsb_fdr(bms_sb_t *sb, int which, xmlNode *node)
{
	xmlNode *item;
	int count, ret;
	hvsb_fdr_t *fdr;

	assert(which == SB_FDR_LIST_IN || which == SB_FDR_LIST_OUT);

	if (!(fdr = (hvsb_fdr_t *)malloc(sizeof(hvsb_fdr_t)))) {
		return OBIX_ERR_NO_MEMORY;
	}
	memset(fdr, 0, sizeof(hvsb_fdr_t));

	if (!(fdr->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME))) {
		goto failed;
	}

	INIT_LIST_HEAD(&fdr->list);

	for (count = 0, item = node->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST KW) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kW);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name, BAD_CAST KWH) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kWh);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		}
	}

	if (count == HVSB_FDR_ATTRIB_MIN) {
		list_add_tail(&fdr->list, &sb->fdrs[which]);
		return OBIX_SUCCESS;
	}

failed:
	log_error("Failed to setup %s fdr %s", sb->type, fdr->name);
	sb->destroy_fdr(fdr);
	return OBIX_ERR_INVALID_ARGUMENT;
}

/*
 * NOTE: msb_fdr_t.kWh[] contains more number of bms_mtr_t than
 * needed for a MSB output feeder, which only requires up to just
 * 2 kWh readings. Therefore all bms_mtr_t.type should be initialised
 * as invalid so as to avoid reading from unused bms_mtr_t.value when
 * calculating the overall kWh readings in the calltrace of
 * get_msb_fdr_kwh > get_mtr_reading.
 *
 * Otherwise valgrind will thrown out error messages related with
 * reading a kWh value not properly generated due to access to the
 * uninitialised meter descriptors.
 */
static int bms_setup_msb_fdr(bms_sb_t *sb, int which, xmlNode *node)
{
	xmlNode *item;
	int count, ret;
	msb_fdr_t *fdr;

	assert(which == SB_FDR_LIST_IN || which == SB_FDR_LIST_OUT);

	if (!(fdr = (msb_fdr_t *)malloc(sizeof(msb_fdr_t)))) {
		return OBIX_ERR_NO_MEMORY;
	}
	memset(fdr, 0, sizeof(msb_fdr_t));

	/*
	 * Initialise the type for each potential kWh readings
	 * as invalid so as to differentiate those not needed
	 * bms_mtr_t in the kWh[] array. See comments above.
	 */
	for (count = 0; count < MSB_FDR_KWH_MAX; count++) {
		fdr->kWh[count].type = MTR_TYPE_MAX;
	}

	if (!(fdr->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME))) {
		goto failed;
	}

	INIT_LIST_HEAD(&fdr->list);

	for (count = 0, item = node->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST KW) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kW);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST msb_fdr_kwh[MSB_FDR_KWH_R1]) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kWh[MSB_FDR_KWH_R1]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST msb_fdr_kwh[MSB_FDR_KWH_R2]) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kWh[MSB_FDR_KWH_R2]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST msb_fdr_kwh[MSB_FDR_KWH_R3]) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kWh[MSB_FDR_KWH_R3]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST msb_fdr_kwh[MSB_FDR_KWH_R4]) == 0) {
			ret = bms_get_csv_settings(item, &fdr->kWh[MSB_FDR_KWH_R4]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		}
	}

	if (count >= MSB_FDR_ATTRIB_MIN) {
		list_add_tail(&fdr->list, &sb->fdrs[which]);
		return OBIX_SUCCESS;
	}

failed:
	log_error("Failed to setup %s FDR %s", sb->type, fdr->name);
	sb->destroy_fdr(fdr);
	return OBIX_ERR_INVALID_ARGUMENT;
}

static int bms_setup_sb_fdrs(bms_sb_t *sb, int which, xmlNode *node)
{
	xmlNode *list, *item;
	int count;

	if (!(list = xml_find_child(node, OBIX_OBJ_LIST, OBIX_ATTR_NAME,
								sb_fdr_list[which]))) {
		log_error("Failed to find %s list in %s config settings",
				  sb_fdr_list[which], sb->name);
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	for (count = 0, item = list->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST OBIX_OBJ) != 0) {
			continue;
		}

		if (sb->setup_fdr(sb, which, item) != OBIX_SUCCESS) {
			log_error("Failed to create descriptor for a %s on %s",
					  which, sb->name);
			return OBIX_ERR_INVALID_ARGUMENT;
		}

		count++;
	}

	/* In case the feeder list is empty */
	return (count > 0) ? OBIX_SUCCESS : OBIX_ERR_INVALID_ARGUMENT;
}

static xmlNode *bms_set_hist_fdr(xmlNode *temp, char *name,
								 const float kw, const float kwh)
{
	xmlNode *copy, *kw_n, *kwh_n;
	char buf[FLOAT_MAX_BITS + 1];

	if (!(copy = xmlCopyNode(temp, 1))) {
		log_error("Failed to clone history template for %s", name);
		return NULL;
	}
	xmlUnlinkNode(copy);

	if (!(kw_n = xml_find_child(copy, OBIX_OBJ_REAL, OBIX_ATTR_NAME, KW)) ||
		!(kwh_n = xml_find_child(copy, OBIX_OBJ_REAL, OBIX_ATTR_NAME, KWH))) {
		log_error("No %s or %s tags in history template for %s",
				  KW, KWH, name);
		goto failed;
	}

	if (!xmlSetProp(copy, BAD_CAST OBIX_ATTR_NAME, BAD_CAST name)) {
		log_error("Failed to replace name in history record of %s", name);
		goto failed;
	}

	sprintf(buf, FORMAT_FLOAT, kw);
	if (!xmlSetProp(kw_n, BAD_CAST OBIX_ATTR_VAL, BAD_CAST buf)) {
		log_error("Failed to set %s value in history record of %s",
				  KW, name);
		goto failed;
	}

	sprintf(buf, FORMAT_FLOAT, kwh);
	if (!xmlSetProp(kwh_n, BAD_CAST OBIX_ATTR_VAL, BAD_CAST buf)) {
		log_error("Failed to set %s value in history record of %s",
				  KWH, name);
		goto failed;
	}

	return copy;

failed:
	xml_delete_node(copy);
	return NULL;
}

static int bms_setup_hvsb_hist(xmlNode *ifdrs, xmlNode *ofdrs,
							   xmlNode *fdr, bms_sb_t *sb)
{
	xmlNode *copy;
	float kw, kwh;
	hvsb_fdr_t *n;

	list_for_each_entry(n, &sb->fdrs[SB_FDR_LIST_IN], list) {
		kw = kwh = 0;
		get_mtr_reading(&n->kW, &kw);
		get_mtr_reading(&n->kWh, &kwh);

		if (!(copy = bms_set_hist_fdr(fdr, n->name, kw, kwh)) ||
			!xmlAddChild(ifdrs, copy)) {
			log_error("Failed to add history record of %s on %s",
					  n->name, sb->name);
			return OBIX_ERR_NO_MEMORY;
		}
	}

	list_for_each_entry(n, &sb->fdrs[SB_FDR_LIST_OUT], list) {
		kw = kwh = 0;
		get_mtr_reading(&n->kW, &kw);
		get_mtr_reading(&n->kWh, &kwh);

		if (!(copy = bms_set_hist_fdr(fdr, n->name, kw, kwh)) ||
			!xmlAddChild(ofdrs, copy)) {
			log_error("Failed to add history record of %s on %s",
					  n->name, sb->name);
			return OBIX_ERR_NO_MEMORY;
		}
	}

	return OBIX_SUCCESS;
}

static int bms_setup_msb_hist(xmlNode *ifdrs, xmlNode *ofdrs,
							  xmlNode *fdr, bms_sb_t *sb)
{
	xmlNode *copy;
	float kw, kwh;
	msb_fdr_t *n;

	list_for_each_entry(n, &sb->fdrs[SB_FDR_LIST_IN], list) {
		kw = kwh = 0;
		get_mtr_reading(&n->kW, &kw);
		get_msb_fdr_kwh(n->kWh, MSB_FDR_KWH_MAX, &kwh);

		if (!(copy = bms_set_hist_fdr(fdr, n->name, kw, kwh)) ||
			!xmlAddChild(ifdrs, copy)) {
			log_error("Failed to add history record of %s on %s",
					  n->name, sb->name);
			return OBIX_ERR_NO_MEMORY;
		}
	}

	list_for_each_entry(n, &sb->fdrs[SB_FDR_LIST_OUT], list) {
		kw = kwh = 0;
		get_mtr_reading(&n->kW, &kw);
		get_msb_fdr_kwh(n->kWh, MSB_FDR_KWH_MAX, &kwh);

		if (!(copy = bms_set_hist_fdr(fdr, n->name, kw, kwh)) ||
			!xmlAddChild(ofdrs, copy)) {
			log_error("Failed to add history record of %s on %s",
					  n->name, sb->name);
			return OBIX_ERR_NO_MEMORY;
		}
	}

	return OBIX_SUCCESS;
}

static bms_sb_t *bms_setup_sb_core(obix_bms_t *bms, int which, xmlNode *node)
{
	bms_sb_t *sb;
	int i;

	if (!(sb = (bms_sb_t *)malloc(sizeof(bms_sb_t)))) {
		return NULL;
	}
	memset(sb, 0, sizeof(bms_sb_t));

	sb->type = which;

	if (!(sb->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME)) ||
		link_pathname(&sb->history_name, bms->history_name, NULL,
					  sb->name, NULL) < 0 ||
		link_pathname(&sb->href, bms->href, bms_sb_list[which],
					  sb->name, NULL) < 0) {
		goto failed;
	}

	for (i = 0; i < SB_FDR_LIST_MAX; i++) {
		INIT_LIST_HEAD(&sb->fdrs[i]);
	}

	INIT_LIST_HEAD(&sb->list);

	switch(sb->type) {
	case BMS_SB_LIST_HVSB:
		sb->setup_fdr = bms_setup_hvsb_fdr;
		sb->destroy_fdr = bms_destroy_hvsb_fdr;
		sb->destroy_sb = bms_destroy_hvsb;
		sb->for_each_fdr = for_each_hvsb_fdr;
		sb->setup_hist = bms_setup_hvsb_hist;
		break;
	case BMS_SB_LIST_MSB:
		sb->setup_fdr = bms_setup_msb_fdr;
		sb->destroy_fdr = bms_destroy_msb_fdr;
		sb->destroy_sb = bms_destroy_msb;
		sb->for_each_fdr = for_each_msb_fdr;
		sb->setup_hist = bms_setup_msb_hist;
		break;
	default:
		break;
	}

	return sb;

failed:
	bms_destroy_sb_core(sb);
	return NULL;
}

static int bms_setup_sbs_helper(xmlNode *node, void *arg1, void *arg2)
{
	obix_bms_t *bms = (obix_bms_t *)arg1;
	BMS_SB_LIST which = *(BMS_SB_LIST *)arg2;
	bms_sb_t *sb;
	int ret, i;

	if (!(sb = bms_setup_sb_core(bms, which, node))) {
		return OBIX_ERR_NO_MEMORY;
	}

	for (i = 0; i < SB_FDR_LIST_MAX; i++) {
		if ((ret = bms_setup_sb_fdrs(sb, i, node)) != OBIX_SUCCESS) {
			goto failed;
		}
	}

	list_add_tail(&sb->list, &bms->sbs[which]);
	return OBIX_SUCCESS;

failed:
	sb->destroy_sb(sb);
	return ret;
}

static int bms_setup_sbs(obix_bms_t *bms, int which, xml_config_t *config)
{
	int ret;

	assert(which == BMS_SB_LIST_HVSB || which == BMS_SB_LIST_MSB);

	if ((ret = xml_config_for_each_obj(config, xp_sbs[which],
									   bms_setup_sbs_helper,
									   bms, &which) != OBIX_SUCCESS)) {
		bms_destroy_sbs(bms, which);
	}

	return ret;
}

static int bms_setup_btank(xmlNode *node, void *arg1, void *arg2)
{
	obix_bms_t *bms = (obix_bms_t *)arg1;	/* arg2 ignored */
	bms_btank_t *btank;
	xmlNode *item;

	if (!(btank = (bms_btank_t *)malloc(sizeof(bms_btank_t)))) {
		return OBIX_ERR_NO_MEMORY;
	}
	memset(btank, 0, sizeof(bms_btank_t));

	if (!(btank->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME))) {
		goto failed;
	}

	INIT_LIST_HEAD(&btank->list);

	for (item = node->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST LEVEL) == 0 &&
			bms_get_csv_settings(item, &btank->level) == OBIX_SUCCESS) {
			list_add_tail(&btank->list, &bms->btanks);
			return OBIX_SUCCESS;
		}
	}

	/* Fall through if no LEVEL tag found, illegal or failed to read */

failed:
	log_error("Failed to setup bulk tank %s", btank->name);
	bms_destroy_btank(btank);
	return OBIX_ERR_INVALID_ARGUMENT;
}

static int bms_setup_btanks(obix_bms_t *bms, xml_config_t *config)
{
	int ret;

	if ((ret = xml_config_for_each_obj(config, XP_BTANKS,
									   bms_setup_btank,
									   bms, NULL)) != OBIX_SUCCESS) {
		bms_destroy_btanks(bms);
	}

	return ret;
}

static int bms_setup_dtank(xmlNode *node, void *arg1, void *arg2)
{
	obix_bms_t *bms = (obix_bms_t *)arg1;	/* arg2 ignored */
	bms_dtank_t *dtank;
	xmlNode *item;
	int count, ret;

	if (!(dtank = (bms_dtank_t *)malloc(sizeof(bms_dtank_t)))) {
		return OBIX_ERR_NO_MEMORY;
	}
	memset(dtank, 0, sizeof(bms_dtank_t));

	if (!(dtank->name = (char *)xmlGetProp(node, BAD_CAST OBIX_ATTR_NAME))) {
		goto failed;
	}

	INIT_LIST_HEAD(&dtank->list);

	for (count = 0, item = node->children; item; item = item->next) {
		if (item->type != XML_ELEMENT_NODE) {
			continue;
		}

		if (xmlStrcmp(item->name, BAD_CAST dtank_lvl[DTANK_LVL_10]) == 0) {
			ret = bms_get_csv_settings(item, &dtank->levels[DTANK_LVL_10]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST dtank_lvl[DTANK_LVL_25]) == 0) {
			ret = bms_get_csv_settings(item, &dtank->levels[DTANK_LVL_25]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST dtank_lvl[DTANK_LVL_50]) == 0) {
			ret = bms_get_csv_settings(item, &dtank->levels[DTANK_LVL_50]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		} else if (xmlStrcmp(item->name,
							 BAD_CAST dtank_lvl[DTANK_LVL_98]) == 0) {
			ret = bms_get_csv_settings(item, &dtank->levels[DTANK_LVL_98]);
			count += (ret == OBIX_SUCCESS) ? 1 : 0;
		}
	}

	if (count == DTANK_LVL_MAX) {
		list_add_tail(&dtank->list, &bms->dtanks);
		return OBIX_SUCCESS;
	}

failed:
	log_error("Failed to setup day tank %s", dtank->name);
	bms_destroy_dtank(dtank);

	/*
	 * Must return an explicit error code instead of ret,
	 * since an error code can be overridden by a success
	 * when one legitimate tag is found
	 */
	return OBIX_ERR_INVALID_ARGUMENT;
}

static int bms_setup_dtanks(obix_bms_t *bms, xml_config_t *config)
{
	int ret;

	if ((ret = xml_config_for_each_obj(config, XP_DTANKS,
									   bms_setup_dtank,
									   bms, NULL)) != OBIX_SUCCESS) {
		bms_destroy_dtanks(bms);
	}

	return ret;
}

/*
 * Read meta settings from the device configuration file
 */
static int bms_setup_param(obix_bms_t *bms, xml_config_t *config)
{
	int ret = OBIX_ERR_INVALID_ARGUMENT;

	if (!(bms->name = xml_config_get_str(config, XP_BMS_ID)) ||
		!(bms->parent_href = xml_config_get_str(config, XP_PARENT_HREF)) ||
		!(bms->history_root = xml_config_get_str(config, XP_HISTORY_ROOT)) ||
		!(bms->csv_dir = xml_config_get_str(config, XP_CSV_DIR)) ||
		!(bms->csv_prefix = xml_config_get_str(config, XP_CSV_PREFIX)) ||
		!(bms->csv_suffix = xml_config_get_str(config, XP_CSV_SUFFIX)) ||
		!(bms->csv_newdir = xml_config_get_str(config, XP_CSV_NEWDIR)) ||
		(bms->updater_period = xml_config_get_int(config, XP_UPDATER_PERIOD)) < 0) {
		log_error("Failed to get BMS settings from config file");
		goto failed;
	}

	if (link_pathname(&bms->history_name, bms->history_root, NULL,
					  bms->name, NULL) < 0 ||
		link_pathname(&bms->href, OBIX_DEVICE_ROOT, bms->parent_href,
					  bms->name, NULL) < 0) {
		log_error("Failed to assemble BMS device href or history_name");
		ret = OBIX_ERR_NO_MEMORY;
		goto failed;
	}

	return OBIX_SUCCESS;

failed:
	bms_destroy_param(bms);
	return ret;
}

/*
 * Setup descriptors for hardware components at various levels
 * according to the interconnection depicted by the device
 * configuration file
 *
 * Return the address of the newly created obix_bms_t structure
 * on success, NULL otherwise
 */
static obix_bms_t *bms_setup_bms(const char *path)
{
	obix_bms_t *bms;
	xml_config_t *config;
	int i;

	if (!(config = xml_config_create(NULL, path))) {
		log_error("%s is not a valid XML file", path);
		return NULL;
	}

	if (!(bms = (obix_bms_t *)malloc(sizeof(obix_bms_t)))) {
		log_error("Failed to malloc software descriptor");
		goto failed;
	}
	memset(bms, 0, sizeof(obix_bms_t));

	/* Setup parameters from the device configuration file */
	if (bms_setup_param(bms, config) != OBIX_SUCCESS) {
		log_error("Failed to setup BMS parameters");
		goto failed;
	}

	if (!(_csv = csv_setup_csv(&bms_csv_ops, BMS_CSV_OPTS, BMS_CSV_DELIM))) {
		log_error("Failed to setup CSV folder descriptor");
		goto failed;
	}

	if (obix_setup_task(&bms->bms_updater, NULL, bms_updater_task, bms,
						bms->updater_period, EXECUTE_INDEFINITE) < 0) {
		log_error("Failed to create bms_updater thread");
		goto failed;
	}

	/* Go on to setup descriptors tree */
	for (i = 0; i < BMS_SB_LIST_MAX; i++) {
		INIT_LIST_HEAD(&bms->sbs[i]);
		if (bms_setup_sbs(bms, i, config) != OBIX_SUCCESS) {
			log_error("Failed to setup %s descriptors", bms_sb_list[i]);
			goto failed;
		}
	}

	INIT_LIST_HEAD(&bms->dtanks);
	INIT_LIST_HEAD(&bms->btanks);

	if (bms_setup_btanks(bms, config) != OBIX_SUCCESS) {
		log_error("Failed to setup BTANK descriptors");
		goto failed;
	}

	if (bms_setup_dtanks(bms, config) != OBIX_SUCCESS) {
		log_error("Failed to setup DTANK descriptors");
		goto failed;
	}

#ifdef DEBUG_CSV
	bms_debug_csv_records(_csv);
#endif

	xml_config_free(config);

	log_debug("Successfully setup BMS descriptor");
	return bms;

failed:
	bms_destroy_bms(bms);

	xml_config_free(config);
	return NULL;
}

static void bms_unregister_sb(bms_sb_t *sb)
{
	obix_unregister_device(OBIX_CONNECTION_ID, sb->history_name);
}

/*
 * Unregister all BMS relevant contracts from the oBIX server
 *
 * NOTE, bulk tanks and day tanks are not registered standalone,
 * so they will get unregistered along with the parent BMS contract.
 * So is the case for any feeders on HVSB or MSB
 */
static void bms_unregister_bms(obix_bms_t *bms)
{
	bms_sb_t *sb;
	int i;

	for (i = 0; i < BMS_SB_LIST_MAX; i++) {
		list_for_each_entry(sb, &bms->sbs[i], list) {
			bms_unregister_sb(sb);
		}
	}

	obix_unregister_device(OBIX_CONNECTION_ID, bms->history_name);
}

static int bms_add_fdr(bms_sb_t *sb, const char *parent_list,
					   const char *name, const float kw, const float kwh,
					   void *arg)	/* arg == bms's address, not used yet */
{
	char *dev_data;
	int len, ret;

	len = strlen(SB_FDR_CONTRACT) + strlen(parent_list) - 2 +
		  (strlen(name) - 2) * 2 + (FLOAT_MAX_BITS - 2) * 2;

	if (!(dev_data = (char *)malloc(len + 1))) {
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(dev_data, SB_FDR_CONTRACT,
			parent_list, name, name,
			kw, kwh);

	ret = obix_write(NULL, OBIX_CONNECTION_ID, sb->history_name,
					 parent_list, dev_data);
	free(dev_data);
	return ret;
}

static int bms_register_sb_core(bms_sb_t *sb)
{
	char *dev_data;
	int len, ret;

	len = strlen(BMS_SB_CONTRACT) + strlen(sb->name) + strlen(sb->href) - 4;

	if (!(dev_data = (char *)malloc(len + 1))) {
		log_error("Failed to allocate contract for %s", sb->history_name);
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(dev_data, BMS_SB_CONTRACT, sb->name, sb->href);

	ret = obix_register_device(OBIX_CONNECTION_ID, sb->history_name, dev_data);
	free(dev_data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register %s", sb->history_name);
		return ret;
	}

	ret = obix_get_history(NULL, OBIX_CONNECTION_ID, sb->history_name);
	if (ret != OBIX_SUCCESS) {
		log_error("Failed to get history facility for %s", sb->history_name);
		bms_unregister_sb(sb);
	}

	return ret;
}

static int bms_register_sb(obix_bms_t *bms, bms_sb_t *sb, const int which)
{
	int ret;

	assert(which == BMS_SB_LIST_HVSB || which == BMS_SB_LIST_MSB);

	if ((ret = bms_register_sb_core(sb)) != OBIX_SUCCESS) {
		log_error("Failed to register %s", sb->name);
		return ret;
	}

	if ((ret = sb->for_each_fdr(sb, bms_add_fdr, bms)) != OBIX_SUCCESS) {
		log_error("Failed to add fdrs on %s", sb->name);
		bms_unregister_sb(sb);
	}

	return ret;
}

static int bms_add_btank(obix_bms_t *bms, bms_btank_t *btank)
{
	char *dev_data;
	uint32_t val = 0;
	int len, ret;
	const char *parent_list = BTANKS;

	len = strlen(BMS_BTANK_CONTRACT) + strlen(parent_list) - 2 +
		  (strlen(btank->name) - 2) * 2 + UINT32_MAX_BITS - 2;

	if (!(dev_data = (char *)malloc(len + 1))) {
		log_error("Failed to allocate contract for %s", btank->name);
		return OBIX_ERR_NO_MEMORY;
	}

	get_mtr_reading(&btank->level, &val);
	sprintf(dev_data, BMS_BTANK_CONTRACT, parent_list,
			btank->name, btank->name, val);

	ret = obix_write(NULL, OBIX_CONNECTION_ID, bms->history_name,
					 parent_list, dev_data);
	free(dev_data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register %s", btank->name);
	}

	return ret;
}

static int bms_add_dtank(obix_bms_t *bms, bms_dtank_t *dtank)
{
	char *dev_data;
	int len, ret;
	const char *parent_list = DTANKS;

	len = strlen(BMS_DTANK_CONTRACT) + strlen(parent_list) - 2 +
		  (strlen(dtank->name) - 2) * 2 +
		  (DTANK_LVL_MAX_BITS - 2) * DTANK_LVL_MAX;

	if (!(dev_data = (char *)malloc(len + 1))) {
		log_error("Failed to allocate contract for %s", dtank->name);
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(dev_data, BMS_DTANK_CONTRACT, parent_list,
			dtank->name, dtank->name,
			lvl_mtr[dtank->levels[DTANK_LVL_10].value.b],
			lvl_mtr[dtank->levels[DTANK_LVL_25].value.b],
			lvl_mtr[dtank->levels[DTANK_LVL_50].value.b],
			lvl_mtr[dtank->levels[DTANK_LVL_98].value.b]);

	ret = obix_write(NULL, OBIX_CONNECTION_ID, bms->history_name,
					 parent_list, dev_data);
	free(dev_data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register %s", dtank->name);
	}

	return ret;
}

static int bms_register_bms(obix_bms_t *bms)
{
	char *dev_data;
	int len, ret, i;
	bms_sb_t *sb;
	bms_btank_t *btank;
	bms_dtank_t *dtank;

	len = strlen(BMS_MESN_CONTRACT) + strlen(bms->name) +
		  strlen(bms->href) + HIST_REC_TS_MAX_LEN - 6;

	if (!(dev_data = (char *)malloc(len + 1))) {
		log_error("Failed to allocate contract for %s", bms->name);
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(dev_data, BMS_MESN_CONTRACT,
			bms->name, bms->href,
			HIST_TS_INIT);

	ret = obix_register_device(OBIX_CONNECTION_ID, bms->history_name, dev_data);
	free(dev_data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to register %s", bms->name);
		return ret;
	}

	ret = obix_get_history(NULL, OBIX_CONNECTION_ID, bms->history_name);
	if (ret != OBIX_SUCCESS) {
		log_error("Failed to create history facility for %s",
				  bms->history_name);
		goto failed;
	}

	ret = obix_get_history_end_ts(NULL, OBIX_CONNECTION_ID, bms->history_name,
								  &bms->mtime_ts);
	if (ret != OBIX_SUCCESS && !(bms->mtime_ts = strdup(HIST_TS_INIT))) {
		log_error("Failed to initialise the latest history timestamp of %s",
				  bms->history_name);
		goto failed;
	}

	log_debug("The latest history TS was %s", bms->mtime_ts);

	for (i = 0; i < BMS_SB_LIST_MAX; i++) {
		list_for_each_entry(sb, &bms->sbs[i], list) {
			if ((ret = bms_register_sb(bms, sb, i)) != OBIX_SUCCESS) {
				goto failed;
			}
		}
	}

	list_for_each_entry(btank, &bms->btanks, list) {
		if ((ret = bms_add_btank(bms, btank)) != OBIX_SUCCESS) {
			goto failed;
		}
	}

	list_for_each_entry(dtank, &bms->dtanks, list) {
		if ((ret = bms_add_dtank(bms, dtank)) != OBIX_SUCCESS) {
			goto failed;
		}
	}

	return OBIX_SUCCESS;

failed:
	bms_unregister_bms(bms);
	return ret;
}

static int bms_append_history_sb(obix_bms_t *bms, bms_sb_t *sb)
{
	xmlNode *sb_n, *fdr, *ifdrs, *ofdrs, *ts;
	char *data;
	int ret = OBIX_SUCCESS;

	sb_n = fdr = ifdrs = ofdrs = ts = NULL;
	xml_config_for_each_obj(bms->history, XP_HIST_SB,
							xml_config_get_template, &sb_n, NULL);
	xml_config_for_each_obj(bms->history, XP_HIST_SB_IFDRS,
							xml_config_get_template, &ifdrs, NULL);
	xml_config_for_each_obj(bms->history, XP_HIST_SB_OFDRS,
							xml_config_get_template, &ofdrs, NULL);
	xml_config_for_each_obj(bms->history, XP_HIST_FDR,
							xml_config_get_template, &fdr, NULL);
	xml_config_for_each_obj(bms->history, XP_HIST_SB_TS,
							xml_config_get_template, &ts, NULL);

	if (!sb_n || !fdr || !ifdrs || !ofdrs || !ts) {
		log_error("Failed to find history templates");
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	/* Wipe out existing fdrs in the subtree */
	xml_remove_children(ifdrs);
	xml_remove_children(ofdrs);

	/* Fill in feeders contracts based on the given device */
	if ((ret = sb->setup_hist(ifdrs, ofdrs, fdr, sb)) != OBIX_SUCCESS) {
		return ret;
	}

	if (!xmlSetProp(ts, BAD_CAST OBIX_ATTR_VAL, BAD_CAST bms->mtime_ts)) {
		log_error("Failed to set TS value in history record of %s", sb->name);
		return OBIX_ERR_NO_MEMORY;
	}

	if (!(data = xml_dump_node(sb_n))) {
		log_error("Failed to dump content of history record of %s", sb->name);
		return OBIX_ERR_NO_MEMORY;
	}

	ret = obix_append_history(NULL, OBIX_CONNECTION_ID,
							  sb->history_name, data);
	free(data);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to append history record for %s", sb->name);
	}

	return ret;
}

static xmlNode *bms_set_hist_btank(bms_btank_t *dev, xmlNode *temp)
{
	xmlNode *copy, *level;
	char buf[UINT32_MAX_BITS + 1];
	uint32_t val = 0;

	if (!(copy = xmlCopyNode(temp, 1))) {
		log_error("Failed to clone history template for %s", dev->name);
		return NULL;
	}
	xmlUnlinkNode(copy);

	if (!(level = xml_find_child(copy, OBIX_OBJ_INT, OBIX_ATTR_NAME, LEVEL))) {
		log_error("No %s tag in history template for %s",
				  LEVEL, dev->name);
		goto failed;
	}

	if (!xmlSetProp(copy, BAD_CAST OBIX_ATTR_NAME, BAD_CAST dev->name)) {
		log_error("Failed to replace name in history record of %s", dev->name);
		goto failed;
	}

	get_mtr_reading(&dev->level, &val);
	sprintf(buf, FORMAT_INT, val);

	if (!xmlSetProp(level, BAD_CAST OBIX_ATTR_VAL, BAD_CAST buf)) {
		log_error("Failed to set %s value in history record of %s",
				  LEVEL, dev->name);
		goto failed;
	}

	return copy;

failed:
	xml_delete_node(copy);
	return NULL;
}

static xmlNode *bms_set_hist_dtank(bms_dtank_t *dev, xmlNode *temp)
{
	xmlNode *copy, *levels[DTANK_LVL_MAX];
	int i;
	LVL_MTR val = LVL_OFF;

	if (!(copy = xmlCopyNode(temp, 1))) {
		log_error("Failed to clone history template for %s", dev->name);
		return NULL;
	}
	xmlUnlinkNode(copy);

	for (i = 0; i < DTANK_LVL_MAX; i++) {
		if (!(levels[i] = xml_find_child(copy, OBIX_OBJ_STR,
										 OBIX_ATTR_NAME, dtank_lvl[i]))) {
			log_error("No %s tag in history template for %s",
					  dtank_lvl[i], dev->name);
			goto failed;
		}
	}

	if (!xmlSetProp(copy, BAD_CAST OBIX_ATTR_NAME, BAD_CAST dev->name)) {
		log_error("Failed to replace name in history record of %s", dev->name);
		goto failed;
	}

	for (i = 0; i < DTANK_LVL_MAX; i++) {
		get_mtr_reading(dev->levels + i, &val);
		if (!xmlSetProp(levels[i], BAD_CAST OBIX_ATTR_VAL,
						BAD_CAST lvl_mtr[val])) {
			log_error("Failed to set %s value in history record of %s",
					  dtank_lvl[i], dev->name);
			goto failed;
		}
	}

	return copy;

failed:
	xml_delete_node(copy);
	return NULL;
}

static int bms_append_history_bms(obix_bms_t *dev)
{
	xmlNode *bms, *btank, *dtank, *btanks, *dtanks, *ts;
	xmlNode *btank_copy, *dtank_copy;
	bms_btank_t *n;
	bms_dtank_t *m;
	char *data;
	int ret = OBIX_ERR_NO_MEMORY;

	bms = btank = btanks = dtank = dtanks = ts = NULL;
	xml_config_for_each_obj(dev->history, XP_HIST_BMS,
							xml_config_get_template, &bms, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_BMS_BTANKS,
							xml_config_get_template, &btanks, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_BMS_DTANKS,
							xml_config_get_template, &dtanks, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_BMS_TS,
							xml_config_get_template, &ts, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_BTANK,
							xml_config_get_template, &btank, NULL);
	xml_config_for_each_obj(dev->history, XP_HIST_DTANK,
							xml_config_get_template, &dtank, NULL);

	if (!bms || !btanks || !dtanks || !ts || !btank || !dtank) {
		log_error("Failed to find history templates for BMS");
		return OBIX_ERR_INVALID_ARGUMENT;
	}

	/* Update bulk tanks in the template */
	xml_remove_children(btanks);
	list_for_each_entry(n, &dev->btanks, list) {
		if (!(btank_copy = bms_set_hist_btank(n, btank)) ||
			!xmlAddChild(btanks, btank_copy)) {
			log_error("Failed to add history record of %s on %s",
					  n->name, dev->name);
			goto failed;
		}
	}

	/* Update day tanks in the template */
	xml_remove_children(dtanks);
	list_for_each_entry(m, &dev->dtanks, list) {
		if (!(dtank_copy = bms_set_hist_dtank(m, dtank)) ||
			!xmlAddChild(dtanks, dtank_copy)) {
			log_error("Failed to add history record of %s on %s",
					  m->name, dev->name);
			goto failed;
		}
	}

	if (!xmlSetProp(ts, BAD_CAST OBIX_ATTR_VAL, BAD_CAST dev->mtime_ts)) {
		log_error("Failed to set TS value in history record of %s", dev->name);
		goto failed;
	}

	if (!(data = xml_dump_node(bms))) {
		log_error("Failed to dump content of history record of %s", dev->name);
		goto failed;
	}

	ret = obix_append_history(NULL, OBIX_CONNECTION_ID,
							  dev->history_name, data);
	free(data);

	return ret;

failed:
	/*
	 * Already added history records will be removed
	 * along with the whole history template during exit
	 */
	if (btank_copy) {
		xml_delete_node(btank_copy);
	}

	if (dtank_copy) {
		xml_delete_node(dtank_copy);
	}

	return ret;
}

static int bms_append_history(obix_bms_t *bms)
{
	bms_sb_t *sb;
	int ret, i;

	for (i = 0; i < BMS_SB_LIST_MAX; i++) {
		list_for_each_entry(sb, &bms->sbs[i], list) {
			if ((ret = bms_append_history_sb(bms, sb)) != OBIX_SUCCESS) {
				return ret;
			}
		}
	}

	if ((ret = bms_append_history_bms(bms)) != OBIX_SUCCESS) {
		log_error("Failed to append history record for %s", bms->name);
	}

	return ret;
}

static int bms_update_fdr(bms_sb_t *sb, const char *parent_list,
						  const char *name, const float kw, const float kwh,
						  void *arg)
{
	Batch *batch = (Batch *)arg;
	char buf[FLOAT_MAX_BITS + 1];
	int ret;
	char *uri;

	if (link_pathname(&uri, parent_list, name, KW, NULL) < 0) {
		log_error("Failed to assemble relative uri for %s on %s",
				  name, sb->name);
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(buf, FORMAT_FLOAT, kw);
	ret = obix_batch_write_value(batch, sb->history_name, uri, buf, OBIX_T_REAL);
	free(uri);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to append batch command for %s on %s",
				  name, sb->name);
		return ret;
	}

	if (link_pathname(&uri, parent_list, name, KWH, NULL) < 0) {
		log_error("Failed to assemble relative uri for %s on %s",
				  name, sb->name);
		return OBIX_ERR_NO_MEMORY;
	}

	sprintf(buf, FORMAT_FLOAT, kwh);
	ret = obix_batch_write_value(batch, sb->history_name, uri, buf, OBIX_T_REAL);
	free(uri);

	if (ret != OBIX_SUCCESS) {
		log_error("Failed to append batch command for %s on %s",
				  name, sb->name);
	}

	return ret;
}

static int bms_update_sb(obix_bms_t *bms, bms_sb_t *sb, const int which)
{
	Batch *batch;
	int ret;

	assert(which == BMS_SB_LIST_HVSB || which == BMS_SB_LIST_MSB);

	if (!(batch = obix_batch_create(OBIX_CONNECTION_ID))) {
		log_error("Failed to create batch object");
		return OBIX_ERR_NO_MEMORY;
	}

	if ((ret = sb->for_each_fdr(sb, bms_update_fdr, batch)) != OBIX_SUCCESS ||
		(ret = obix_batch_send(NULL, batch)) != OBIX_SUCCESS) {
		log_error("Failed to update %s via batch object", sb->name);
	}

	obix_batch_destroy(batch);
	return ret;
}

static int bms_update_btank(obix_bms_t *bms, bms_btank_t *btank,
							Batch *batch)

{
	char buf[UINT32_MAX_BITS + 1];
	char *uri;
	int ret;
	uint32_t val = 0;

	if (link_pathname(&uri, BTANKS, btank->name, LEVEL, NULL) < 0) {
		log_error("Failed to assemble relative uri for %s on %s",
				  LEVEL, btank->name);
		return OBIX_ERR_NO_MEMORY;
	}

	get_mtr_reading(&btank->level, &val);
	sprintf(buf, FORMAT_INT, val);

	ret = obix_batch_write_value(batch, bms->history_name, uri, buf, OBIX_T_INT);
	free(uri);

	return ret;
}

static int bms_update_dtank(obix_bms_t *bms, bms_dtank_t *dtank,
							Batch *batch)
{
	char *uri;
	int ret = OBIX_SUCCESS, i;
	LVL_MTR val = LVL_OFF;

	for (i = 0; i < DTANK_LVL_MAX; i++) {
		if (link_pathname(&uri, DTANKS, dtank->name, dtank_lvl[i], NULL) < 0) {
			log_error("Failed to assemble relative uri for %s on %s",
					  dtank_lvl[i], dtank->name);
			return OBIX_ERR_NO_MEMORY;
		}

		get_mtr_reading(dtank->levels + i, &val);
		ret |= obix_batch_write_value(batch, bms->history_name, uri,
									 lvl_mtr[val], OBIX_T_STR);
		free(uri);
	}

	return ret;
}

/*
 * Traverse the whole descriptors tree, have relevant contract
 * on the oBIX server updated for each device
 */
static int bms_update_bms(obix_bms_t *bms)
{
	bms_sb_t *sb;
	bms_btank_t *btank;
	bms_dtank_t *dtank;
	Batch *batch = NULL;
	int i, ret;

	for (i = 0; i < BMS_SB_LIST_MAX; i++) {
		list_for_each_entry(sb, &bms->sbs[i], list) {
			if ((ret = bms_update_sb(bms, sb, i)) != OBIX_SUCCESS) {
				log_error("Failed to update %s", sb->name);
				return ret;
			}
		}
	}

	/*
	 * Use a separate batch object to update bulk tanks,
	 * day tanks and mtime on BMS contract
	 */

	if (!(batch = obix_batch_create(OBIX_CONNECTION_ID))) {
		log_error("Failed to create batch object");
		return OBIX_ERR_NO_MEMORY;
	}

	list_for_each_entry(btank, &bms->btanks, list) {
		if ((ret = bms_update_btank(bms, btank, batch)) != OBIX_SUCCESS) {
			log_error("Failed to update %s", btank->name);
			goto failed;
		}
	}

	list_for_each_entry(dtank, &bms->dtanks, list) {
		if ((ret = bms_update_dtank(bms, dtank, batch)) != OBIX_SUCCESS) {
			log_error("Failed to update %s", dtank->name);
			goto failed;
		}
	}

	ret = obix_batch_write_value(batch, bms->history_name, BMS_MTIME,
					(bms->mtime_ts) ? bms->mtime_ts : HIST_TS_INIT,
					OBIX_T_ABSTIME);
	if (ret != OBIX_SUCCESS) {
		log_error("Failed to append batch command for %s on %s",
				  BMS_MTIME, bms->name);
		goto failed;
	}

	if ((ret = obix_batch_send(NULL, batch)) != OBIX_SUCCESS) {
		log_error("Failed to update %s via batch object", bms->name);
	}

	/* Fall through */

failed:
	if (batch) {
		obix_batch_destroy(batch);
	}

	return ret;
}

static void bms_reloc_csv_file(obix_bms_t *bms, csv_file_t *file)
{
	char *newpath = NULL;
	const char *filename = file->path + strlen(bms->csv_dir);

	/*
	 * If no new directory is specified for handled CSV files,
	 * then they are simply deleted
	 */
	if (strlen(bms->csv_newdir) == 0) {
		errno = 0;
		if (unlink(file->path) < 0) {
			log_error("%s: failed to delete %s", strerror(errno), filename);
		}

		return;
	}

	/*
	 * Get the new path for handled CSV file by replacing old
	 * directory with the new one
	 */
	if (link_pathname(&newpath, bms->csv_newdir, NULL, filename, NULL) < 0) {
		log_error("Failed to assemble the path of the new directory");
		return ;
	}

	errno = 0;
	if (rename(file->path, newpath) < 0) {
		log_error("%s: failed to move %s to %s", strerror(errno),
				  filename, bms->csv_newdir);
	}

	free(newpath);
}

/*
 * The workload of the bms updater thread includes the following
 * tasks:
 * . Organise existing CSV files according to their creation time
 * . Read records from one CSV file into descriptors
 * . Append history records for the current CSV file
 * . Repeat above two steps until all CSV files are processed
 * . Update device contracts based on the last CSV file in the queue
 *   (the most recently generated)
 *
 * NOTE: The queue of file descriptors will always be released
 * before the work thread finishes the current round of execution,
 * just in case any of them get invalidated due to the removal of
 * CSV files.
 *
 * NOTE: Existing CSV files are read before waiting for any async
 * inotify events. So in theory there is still a chance for the very
 * first run of the updater thread to race with BMS's write operation,
 * in which case the file size could be zero, then the updater thread
 * will break from the loop and wait for relevant IN_CLOSE_WRITE event
 * and read it out in the next run.
 */
static void bms_updater_task(void *arg)
{
	obix_bms_t *bms = (obix_bms_t *)arg;
	csv_file_t *file;
	char *ts = NULL;
	int res_d, res_t;
	struct inotify_event *event;
	char buf[BMS_INOTIFY_BUFLEN]__attribute__((aligned(4)));
	int len, i;

	if (for_each_file_name(bms->csv_dir, bms->csv_prefix, bms->csv_suffix,
						   csv_setup_file, &_csv->files) < 0) {
		log_error("Failed to sort out existing CSV files");
		goto failed;
	}

	list_for_each_entry(file, &_csv->files, list) {
		if (ts) {
			free(ts);
			ts = NULL;
		}

		/*
		 * Compare each existing CSV file's timestamp with the timestamp
		 * of the last handled CSV file
		 */
		if (!(ts = obix_get_timestamp(file->mtime)) ||
			timestamp_compare(ts, bms->mtime_ts, &res_d, &res_t) < 0) {
			log_error("Failed to compare timestamp for %s", file->path);
			break;
		}

		if (res_d < 0 || (res_d == 0 && res_t <= 0)) {
			log_debug("%s skipped", file->path + strlen(bms->csv_dir));
			log_debug("its timestamp: %s", ts);
			log_debug("while last CSV file's timestamp: %s\n", bms->mtime_ts);
			bms_reloc_csv_file(bms, file);
			continue;
		}

		if (bms->mtime_ts) {
			free(bms->mtime_ts);
		}

		if (!(bms->mtime_ts = strdup(ts))) {
			log_error("Failed to duplicate timestamp string of %s", file->path);
			break;
		}

		if (csv_read_file(_csv, file) != OBIX_SUCCESS) {
			/*
			 * If failed to read from a CSV file, break from the loop
			 * and not rename/delete it so that it can have a chance
			 * to be read again in the next run
			 */
			break;
		}

		bms_reloc_csv_file(bms, file);

		if (bms_append_history(bms) != OBIX_SUCCESS) {
			log_error("Failed to append history for data from %s", file->path);

			/*
			 * Ignore error so that device contracts may have a chance
			 * to be updated properly
			 */
		}

		/*
		 * Update device contracts on oBIX server only
		 * based on the last/latest CSV file
		 */
		if (list_is_last(&file->list, &_csv->files) == 1) {
			if (bms_update_bms(bms) != OBIX_SUCCESS) {
				log_error("Failed to update contracts by data from %s",
						  file->path);
			}
		}
	}

	/* Fall through */

failed:
	if (ts) {
		free(ts);
	}

	csv_destroy_files(_csv);

	/*
	 * Once all existing CSV files have been handled, blocking
	 * to receive any asynchronous inotify message from kernel
	 * AFTER a new CSV file has been created and closed.
	 *
	 * Upon reception of such message, return so as to kick off
	 * another round of execution to handle it.
	 *
	 * NOTE: if the inotify descriptor received more number of
	 * events than what the provided buf can accommodate, they
	 * still can be properly read out in the loop.
	 *
	 * NOTE: the deletion of the watch and inotify objects will
	 * have kernel send the IN_IGNORED event to the reader, who
	 * needs to return immediately to unblock the main thread
	 * from waiting for its completion.
	 */
	while (1) {
		len = read(bms->fd, buf, BMS_INOTIFY_BUFLEN);

		i = 0;
		while (i < len) {	/* Including failed to read from inotify */
			event = (struct inotify_event *)&buf[i];

			log_debug("mask=%u, cookie=%u, len=%u, name=%s",
					  event->mask, event->cookie, event->len,
					  (event->len > 0) ? event->name : "");

			if (event->mask & IN_IGNORED) {
				log_debug("In process of exiting...");
				return;
			}

			if (event->mask & IN_CLOSE_WRITE) {
				log_debug("New CSV file closed after being written");
				return;
			}

			i += sizeof(struct inotify_event) + event->len;
		}
	}
}

static void bms_destroy_inotify(obix_bms_t *bms)
{
	/*
	 * Inotify and relevant watch descriptors are initialised
	 * as zero since the BMS adaptor needs to read from a couple
	 * of configuration files anyway
	 */
	if (bms->fd > 0) {
		if (bms->wd > 0) {
			errno = 0;
			if (inotify_rm_watch(bms->fd, bms->wd) < 0) {
				log_error("Failed to unload inotify with the watch : %s",
						  strerror(errno));
				return;
			}
		}

		close(bms->fd);
	}
}

static int bms_setup_inotify(obix_bms_t *bms)
{
	int ret = OBIX_SUCCESS;

	errno = 0;
	if ((bms->fd = inotify_init()) < 0) {
		log_error("Failed to setup inotify: %s", strerror(errno));
		return OBIX_ERR_NO_MEMORY;
	}

	errno = 0;
	if ((bms->wd = inotify_add_watch(bms->fd, bms->csv_dir,
									 BMS_INOTIFY_MASK)) < 0) {
		log_error("Failed to load inotify with watch on %s : %s",
				  bms->csv_dir, strerror(errno));
		close(bms->fd);
		ret = OBIX_ERR_NO_MEMORY;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	obix_bms_t *bms;
	int ret = OBIX_SUCCESS;

	if (argc != 4) {
		printf("Usage: %s <devices_config_file> <obix_config_file> "
			   "<history_template_file>\n", argv[0]);
		return -1;
	}

	if (signal(BMS_SIGNAL_CLEANUP, bms_signal_handler) == SIG_ERR) {
		log_error("Failed to register cleanup signal handler");
		return -1;
	}

	xml_parser_init();

	if (!(bms = bms_setup_bms(argv[1]))) {
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto setup_failed;
	}

	if ((ret = obix_setup_connections(argv[2])) != OBIX_SUCCESS) {
		goto obix_failed;
	}

	if ((ret = obix_open_connection(OBIX_CONNECTION_ID)) != OBIX_SUCCESS) {
		goto open_failed;
	}

	if (!(bms->history = xml_config_create(NULL, argv[3]))) {
		log_error("Failed to setup history template: %s not valid", argv[3]);
		ret = OBIX_ERR_INVALID_ARGUMENT;
		goto template_failed;
	}

	if ((ret = bms_register_bms(bms)) != OBIX_SUCCESS) {
		goto register_failed;
	}

	if ((ret = bms_setup_inotify(bms)) != OBIX_SUCCESS) {
		goto inotify_failed;
	}

	if (obix_schedule_task(&bms->bms_updater) < 0) {
		log_error("Failed to start the bms_updater thread");
		ret = OBIX_ERR_BAD_CONNECTION_HW;
		goto task_failed;
	}

	/*
	 * Suspend and wait for a signal sent from command line by human user.
	 * Since human users are not likely to hit the Ctrl+C keys twice with
	 * an interval so short as to have the second signal handler race
	 * against the main thread reading from the flag, there is no need to
	 * block relevant signal while accessing the flag.
	 */
	while (1) {
		pause();

		if (flag_exit == 1) {
			log_debug("Begin to shutdown gracefully...");
			break;
		}
	}

	/* Fall through */

task_failed:
	/*
	 * The inotify and relevant watch objects need to be deleted
	 * so that the child thread can be interrupted, therefore the
	 * main thread here won't have to wait for it to exit until
	 * the arrival of one of the specified event next time.
	 */
	bms_destroy_inotify(bms);
	obix_cancel_task(&bms->bms_updater);

inotify_failed:
	bms_unregister_bms(bms);

register_failed:
	xml_config_free(bms->history);

template_failed:
	obix_destroy_connection(OBIX_CONNECTION_ID);

open_failed:
	obix_destroy_connections();

obix_failed:
	bms_destroy_bms(bms);

setup_failed:
	xml_parser_exit();

	return ret;
}
