				oBIX Adaptor for Modbus Gateway
 
Copyright (c) 2013-2014 Qingtao Cao [harry.cao@nextdc.com]    

This file contains a description of the project in the following sections:

1. Overview
2. Configuration Files
3. Hierarchy Organisations
4. Handy Tool


--1-- Overview

The MGATE adaptor is the oBIX adaptor for the MGATE box which is a modbus master device and converts modbus commands/responses between TCP connection and modbus lines.

The MGATE adaptor manipulates a couple of core data structures as hardware descriptors for MGATE box, modbus line, BCM (Branch Circuit Monitor) device and CB (Circuit Branch Meter) device respectively, which are organized in the same way as depicted by relevant XML config file, which also illustrates hardware parameters, in particular, holding registers layout on BCM device about various analog attributes on the AUX and CB devices such as I, V, PF, kW and kWh.

At start-up, the MGATE adaptor will register all BCM and CB devices on the oBIX server, then update their status as fast as possible and post history records of above power consumption relevant information for each CB device at fixed interval.

One MGATE box can host a number of modbus lines with several BCM devices serialized on each of them. As a proper balance between manipulation of one modbus capacity and speed, it is a common practice to connect 2~3 BCMs on one modbus line.

In theory a MGATE box can serve and respond to requests to different modbus lines simultaneously. To take the most advantage out of it, a pair of producer-consumer threads are spawned for each modbus line and assume different roles to collect latest hardware status via MGATE box and to post them to oBIX server separately.

Each producer thread will have its own modbus context structure exclusively from other producer threads attending different modbus lines, so is the fact that each consumer thread will monopoly its own curl handle from other consumer threads, since both structures are not thread-safe.

If a producer thread finds the timeout counter when reading from a BCM device exceeds the predefined threshold(e.g., 3), which in most cases implies that the BCM has been removed off-line, it would mark the device as is. Consequently, the paired consumer thread would stop posting its status to oBIX server until this flag has been cleared either by administrator on the command line(refer to the comments of mg_resurrect_dev() for details), or by the producer thread when it finds the device is accessible again.

To achieve 'real-time' behaviour when representing hardware status on oBIX server, the period settings for the consumer and producer threads could be set as zero so that they will be running as fast as possible. Then the latency will be mostly comprised of the network latency between MGATE adaptor program and the MOXA box and the oBIX server it talks with on both sides, the latency occurred on MGATE box hardware and that of modbus lines.

Last but not least, the administrator can press Ctrl-C which will emit SIGINT signal to have the whole MGATE adaptor program exit gracefully. 


--2-- Configuration Files

The MGATE adaptor needs two configuration files and can be started by the following command:

	$ <PREFIX>/usr/bin/mg_adaptor
			<PREFIX>/etc/obix/res/adaptors/mg_adaptor_devices_config.xml
			<PREFIX>/etc/obix/res/adaptors/generic_server_config.xml

Both above configuration files help extract and separate hardware configurables and settings away from the mechanism implemented by binaries, so that binaries are hardware-neutral and only these configuration files need to be edited when MOXA boxes are configured differently.

The first configuration file describes hardware connection and configuration information such as the IP address and port number of the MOXA box, the name and slave ID of each BCM device on each modbus line and the layout of holding registers on BCM devices.

The second configuration file captures the connection settings with the oBIX server, the client side limitations such as the maximal number of devices supported and the log facilities used.


--3-- Hierarchy Organisations

The oBIX contracts for BCM and CB devices and history facilities for each CB devices are all organised in a hierarchy structure. In particular, the "device_href" and "history_lobby" settings in the MGATE adaptor's device configuration file specify where all BCM and CB contracts are registered and where history facilities of all CB devices are placed within "/obix/historyService/histories/" lobby:

	<controller_address>
		......
		<history_lobby val="/M1/DH1/"/>
	</controller_address>

	<obj name="4A-1A">
		<int name="slave_id" val="1"/>
		<str name="device_href" val="/M1/DH1/A/"/>
	</obj>

In above example, all devices will have their history facilities created under "/obix/historyService/histories/M1/DH1/", where "M1" and "DH1" stands for the names of data centre and data hall respectively, and the BCM named "4A-1A" will have device contract for itself and all its CBs on-board created under "/obix/deviceRoot/M1/DH1/A/". As a result, its oBIX contract looks like below:

	<obj name="4A-1A" href="/obix/deviceRoot/4A-1A" is="nextdc:VerisBCM">
		<int name="SlaveID" href="SlaveID" val="1"/>
		<int name="SerialNumber" href="SerialNumber" val="0x4e342ef9" writable="true"/>
		<int name="Firmware" href="Firmware" val="0x03ed03f4" writable="true"/>
		<int name="Model" href="Model" val="15172" writable="true"/>
		<int name="CTConfig" href="CTConfig" val="2" writable="true"/>
		<str name="Location" href="Location" val="AUDM1DH4 PDU-4A-1A Panel #1" writable="true"/>
		<real name="ACFreq" href="ACFreq" val="50.000000" writable="true"/>
		<real name="VoltL-N" href="VoltL-N" val="242.080078" writable="true"/>
		<real name="VoltL-L" href="VoltL-L" val="418.952057" writable="true"/>
		<real name="VoltA" href="VoltA" val="240.157227" writable="true"/>
		<real name="VoltB" href="VoltB" val="243.659668" writable="true"/>
		<real name="VoltC" href="VoltC" val="242.563507" writable="true"/>
		<real name="kWh" href="kWh" val="218.000000" writable="true"/>
		<real name="kW" href="kW" val="0.000000" writable="true"/>
		<real name="CurrentAverage" href="CurrentAverage" val="0.000000" writable="true"/>
		<abstime name="LastUpdated" href="LastUpdated" val="2014-05-19T01:19:24" writable="true"/>
		<bool name="Online" href="OnLine" val="true" writable="true"/>
		<list name="Meters" href="Meters" of="nextdc:Meter">
			<obj name="CB01" href="CB01" is="nextdc:Meter">
				<real name="kWh" href="kWh" val="25.444157" writable="true"/>
				<real name="kW" href="kW" val="0.000000" writable="true"/>
				<real name="V" href="V" val="240.157227" writable="true"/>
				<real name="PF" href="PF" val="0.900000" writable="true"/>
				<real name="I" href="I" val="0.000000" writable="true"/>
			</obj>
			<obj name="CB02" href="CB02" is="nextdc:Meter">
				<real name="kWh" href="kWh" val="50.943935" writable="true"/>
				<real name="kW" href="kW" val="0.000000" writable="true"/>
				<real name="V" href="V" val="243.659668" writable="true"/>
				<real name="PF" href="PF" val="0.900000" writable="true"/>
				<real name="I" href="I" val="0.000000" writable="true"/>
			</obj>
			......
		</list>
	</obj>

As illustrated by above example, the href attribute of this BCM device's contract in the global XML storage on the oBIX server is "/obix/deviceRoot/4A-1A", and the oBIX contract for CB devices are all children elements of a "obix:list" object named "Meters" in the BCM device's contract, therefore any single CB device's contract can be addressed by the href of "/obix/deviceRoot/4A-1A/Meters/CBXX", where "XX" is the href or name of this CB device on its parent BCM device.

Similarly, history facilities of all CB devices on a BCM device are children of a general "obix:obj" contract whose href is ended up with the BCM's name, for example:

	<obj href="/obix/historyService/histories/M1/DH1/4A-1A">
		<obj is="obix:HistoryDevLobby" href="CB01">
			<op name="query" href="query" in="obix:HistoryFilter" out="obix:HistoryQueryOut"/>
			<op name="append" href="append" in="obix:HistoryAppendIn" out="obix:HistoryAppendOut"/>
			<list name="index" href="index" of="obix:HistoryFileAbstract">
				<obj is="obix:HistoryFileAbstract">
					<date name="date" val="2014-05-15"/>
					<int name="count" val="1964"/>
					<abstime name="start" val="2014-05-15T06:40:34"/>
					<abstime name="end" val="2014-05-15T23:27:59"/>
				</obj>
				......
			</list>
		</obj>
		<obj is="obix:HistoryDevLobby" href="CB02">
			<op name="query" href="query" in="obix:HistoryFilter" out="obix:HistoryQueryOut"/>
			<op name="append" href="append" in="obix:HistoryAppendIn" out="obix:HistoryAppendOut"/>
			<list name="index" href="index" of="obix:HistoryFileAbstract">
				<obj is="obix:HistoryFileAbstract">
					<date name="date" val="2014-05-15"/>
					<int name="count" val="1964"/>
					<abstime name="start" val="2014-05-15T06:43:26"/>
					<abstime name="end" val="2014-05-15T23:59:35"/>
				</obj>
				......
			</list>
		</obj>
		......
	</obj>

Although BCM "4A-1A" doesn't have a history facility established, its name and names of ancestor data hall and data centre facilities are used to setup a hierarchy organisation of CBs' history facilities.


--4-- Handy Tools

The holding registers on a modbus slave can be directly accessed by modbus-cli tool as the following way:

	modbus read -s <slave ID> <master IP> <addr> <count>

or alternatively, by the modbus.c program in src/tools/ in a similar manner:

	./modbus <master IP> <slave ID> <addr> <count>

Please refer to modbus-cli's help or comments of modbus.c for more information.
