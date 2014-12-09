# Timezone Support

## Supported Timezone Format

The oBIX specification demands a ISO-8601 timezone support. However, according to current glibc implementation of the strptime() API, it recognises only two formats with two or four digits in the timezone string after the '+/-' offset character, and will silently ignore any other unrecognisable formats.

Firstly, if there is any non-digit character or symbol following the first 2 digits, e.g. the colon in a 'hh:mm' format, then strptime() will stop parsing the rest of string and regard it as an 'hh' format.

Secondly, the value of 'hh' should be no bigger than 12, which is understandable since the offset characters are used. In particular, if 'hh' equals 12, then 'mm' must be zero.

Thirdly, the value of 'mm' is calculated as '(mm * 5/3) * 36' seconds. As a result, it will only be interpreted as the number of 'minutes' normally expected by users when it is a multiple of three. For example:

	2014-10-14T10:00:00-0001
	2014-10-14T10:00:00-0002
	2014-10-14T10:00:00-0003

are treated as:

	2014-10-14T10:00:36Z
	2014-10-14T10:01:48Z
	2014-10-14T10:03:00Z

Considering this and the fact that most timezones have an integral difference from UTC, with a few others having a 15 or 30 minute difference (such as South Australia or the Northern Territories of Australia, Nepal etc.), only '00/15/30/45' are allowed in the 'mm' part.

If no timezone designators are provided in the timestamp strings (e.g. all existing history records generated earlier), they will be regarded as in UTC timezone by default. The oBIX server converts all timestamps into UTC timezone before comparison, however, it will not reformat the timestamp in the history records into UTC timezone before saving into raw history raw data files.

If (all or part of) the provided timezone can not be properly interpreted, an error contract is sent back to the relevant clients to notify them of this.

Please refer to the following link regarding the implementation of strptime():
	https://github.com/andikleen/glibc/blob/master/time/strptime_l.c


## Test Case

The src/tools/ts2utc.c program extracts the core timestamp manipulation code and can be used for test purposes. It checks the sanity of the given timestamp and rebases it to the UTC timezone. Even if the sanity check fails, the rebase will be carried out so that users can see how the C library strptime() API may have ignored unrecognisable timezone designator.

Some examples are illustrated below.

```
1. Treat no timezone as in UTC timezone:

	$ ./ts2utc 2012-2-28T20:00:00
	Input timestamp: 2012-2-28T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330459200
	New timestamp: 2012-02-28T20:00:00Z

2. Support "Z" as in UTC timezone:

	$ ./ts2utc 2012-2-28T20:00:00Z
	Input timestamp: 2012-2-28T20:00:00Z, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330459200
	New timestamp: 2012-02-28T20:00:00Z

3. Support leap year and the "+/hhmm" format:

	$ ./ts2utc 2012-2-28T20:00:00-0500
	Input timestamp: 2012-2-28T20:00:00-0500, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1330477200
	New timestamp: 2012-02-29T01:00:00Z
	
	$ ./ts2utc 2013-2-28T20:00:00-0500
	Input timestamp: 2013-2-28T20:00:00-0500, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362099600
	New timestamp: 2013-03-01T01:00:00Z
	
	$ ./ts2utc 2013-2-28T20:00:00+0500
	Input timestamp: 2013-2-28T20:00:00+0500, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362063600
	New timestamp: 2013-02-28T15:00:00Z

4. Only when "mm" is a multiple of 3 can it be treated as the value of "minutes":

	$ ./ts2utc 2013-2-28T20:00:00-0501
	Provided timestamp is invalid, all or part of it will be ignored: 2013-2-28T20:00:00-0501
	Input timestamp: 2013-2-28T20:00:00-0501, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362099636
	New timestamp: 2013-03-01T01:00:36Z
	
	$ ./ts2utc 2013-2-28T20:00:00-0502
	Provided timestamp is invalid, all or part of it will be ignored: 2013-2-28T20:00:00-0502
	Input timestamp: 2013-2-28T20:00:00-0502, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362099708
	New timestamp: 2013-03-01T01:01:48Z
	
	$ ./ts2utc 2013-2-28T20:00:00-0503
	Provided timestamp is invalid, all or part of it will be ignored: 2013-2-28T20:00:00-0503
	Input timestamp: 2013-2-28T20:00:00-0503, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362099780
	New timestamp: 2013-03-01T01:03:00Z
	
	$ ./ts2utc 2013-2-28T20:00:00-0515
	Input timestamp: 2013-2-28T20:00:00-0515, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362100500
	New timestamp: 2013-03-01T01:15:00Z
	
	$ ./ts2utc 2013-2-28T20:00:00-0530
	Input timestamp: 2013-2-28T20:00:00-0530, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362101400
	New timestamp: 2013-03-01T01:30:00Z
	
	$ ./ts2utc 2013-2-28T20:00:00-0545
	Input timestamp: 2013-2-28T20:00:00-0545, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1362102300
	New timestamp: 2013-03-01T01:45:00Z

5. Support of the "hh" format:

	$ ./ts2utc 2012-2-28T20:00:00-05
	Input timestamp: 2012-2-28T20:00:00-05, remnant: , errno: Success
	Calender time in UTC (GMT+0) timezone: 1330477200
	New timestamp: 2012-02-29T01:00:00Z

6. Truncate "hh:mm" to "hh":

	$ ./ts2utc 2012-2-28T20:00:00-05:30
	Provided timestamp is invalid, all or part of it will be ignored: 2012-2-28T20:00:00-05:30
	Input timestamp: 2012-2-28T20:00:00-05:30, remnant: :30, errno: Success
	Calender time in UTC (GMT+0) timezone: 1330477200
	New timestamp: 2012-02-29T01:00:00Z

7. Ignore the entire timezone if "hh" or "mm" containing less than 2 digits:

	$ ./ts2utc 2012-2-28T20:00:00-5:00
	Provided timestamp is invalid, all or part of it will be ignored: 2012-2-28T20:00:00-5:00
	Input timestamp: 2012-2-28T20:00:00-5:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330459200
	New timestamp: 2012-02-28T20:00:00Z

8. Ignore the entire timezone if "hh" or "mm" is larger than 12 or 60 respectively, or overally larger than "1200":

	$ ./ts2utc 2012-2-28T20:00:00-13:00
	Provided timestamp is invalid, all or part of it will be ignored: 2012-2-28T20:00:00-13:00
	Input timestamp: 2012-2-28T20:00:00-13:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330459200
	New timestamp: 2012-02-28T20:00:00Z
	
	$ ./ts2utc 2012-2-28T20:00:00-0560
	Input timestamp: 2012-2-28T20:00:00-0560, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330459200
	New timestamp: 2012-02-28T20:00:00Z

	$ ./ts2utc 2014-10-21T12:48:26-1215
	Provided timestamp is invalid, all or part of it will be ignored: 2014-10-21T12:48:26-1215
	Input timestamp: 2014-10-21T12:48:26-1215, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1413895706
	New timestampe: 2014-10-21T12:48:26Z

```

## Robustness of C APIs

It should be noted that timestamp_is_valid() can only sanity check if a timestamp string is in "%FT%T%z" format. It cannot further validate if a specific field contains a meaningful value, e.g. whether the minute value is larger than 61, or whether February has 28 or 29 days.

It is also important to note how strptime() and timegm() APIs handle invalid values. Sometimes they are converted to valid values, but this will not always happen and they will simply be treated as all zero, or bring about an error code. As a result, the history handler may complain that the given record includes a timestamp older than, or equal to that of the last one.

Some examples are illustrated below.

```
1. Invalid day value (up to 31) can be properly fixed according to whether the year is a leap year or not:

	$ ./ts2utc 2012-2-30T20:00:00
	Input timestamp: 2012-2-30T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330632000
	New timestampe: 2012-03-01T20:00:00Z

	$ ./ts2utc 2014-2-30T20:00:00
	Input timestamp: 2014-2-30T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1393790400
	New timestampe: 2014-03-02T20:00:00Z

2. Support up to 61 in the second field (leap second):

	$  ./ts2utc 2012-2-29T13:59:60
	Input timestamp: 2012-2-29T13:59:60, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330524000
	New timestampe: 2012-02-29T14:00:00Z

	$  ./ts2utc 2012-2-29T13:59:61
	Input timestamp: 2012-2-29T13:59:61, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330524001
	New timestampe: 2012-02-29T14:00:01Z

3. Other invalid values in "%T" part are simply treated as all zero:

	$ ./ts2utc 2012-2-29T13:00:62
	Input timestamp: 2012-2-29T13:00:62, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330473600
	New timestampe: 2012-02-29T00:00:00Z

	$ ./ts2utc 2012-2-29T13:60:00
	Input timestamp: 2012-2-29T13:60:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330473600
	New timestampe: 2012-02-29T00:00:00Z

	$ ./ts2utc 2012-2-29T24:59:61
	Input timestamp: 2012-2-29T24:59:61, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: 1330473600
	New timestampe: 2012-02-29T00:00:00Z

4. Invalid values in "%F" part simply result in error code:

	$ ./ts2utc 2014-2-32T20:00:00
	Input timestamp: 2014-2-32T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: -2209075200
	Failed to convert calender time to timestamp string

	$ ./ts2utc 2014-13-31T20:00:00
	Input timestamp: 2014-13-31T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: -2209075200
	Failed to convert calender time to timestamp string

	$ ./ts2utc 1969-2-28T20:00:00
	Input timestamp: 1969-2-28T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: -26452800
	Failed to convert calender time to timestamp string

	$ ./ts2utc 10000-2-28T20:00:00
	Input timestamp: 10000-2-28T20:00:00, remnant: (null), errno: Success
	Calender time in UTC (GMT+0) timezone: -2209075200
	Failed to convert calender time to timestamp string

```
