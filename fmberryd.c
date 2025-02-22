/*
	FMBerry - an cheap and easy way of transmitting music with your Pi.
    Copyright (C) 2011-2013 by Tobias Mädel (t.maedel@alfeld.de)
	Copyright (C) 2013      by Andrey Chilikin (https://github.com/achilikin)
	Copyright (C) 2021      by Johan Muizelaar - modifications for multiple fm transmitters - (https://github.com/johanmz)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "fmberryd.h"
#include "rpi_pin.h"
#include "ns741.h"
#include "tca9548a.h"
#include "mcp23017.h"

#include <poll.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <confuse.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include "defs.h"

#define RPI_REVISION RPI_REV2

mmr70_data_t mmr70[MAXTRANSMITTERS];
IOexpander_data_t IOexpander[MAXIOEXPANDERS];
multiplexer_data_t multiplexer[MAXMULTIPLEXERS];

static cfg_t *cfg, *cfg_transmitter, *cfg_IOexpander;
static volatile int run = 1;
static int start_daemon = 1;

static int IOexpander_to_mmr70[MAXIOEXPANDERS][16]; // lookup which transmitter belongs to which mcp23017 por, in handling rds interupts
static int nr_transmitters =0; 

int main(int argc, char **argv)
{
	//Check if user == root
	// if(geteuid() != 0)
	// {
	//   puts("Please run this software as root!");
	//   exit(EXIT_FAILURE);
	// }

	// check for non-daemon mode for debugging
	for(int i = 1; i < argc; i++) {
		if (str_is(argv[i], "nodaemon")) {
			start_daemon = 0;
			break;
		}
	}

	if (start_daemon) {
		//Init daemon, can be replaced with daemon() call
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			exit(EXIT_FAILURE);
		}
		if (pid > 0) {
			exit(EXIT_SUCCESS);
		}

		umask(0);

		//We are now running as the forked child process.
		openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER);

		pid_t sid;
		sid = setsid();
		if (sid < 0) {
			syslog(LOG_ERR, "Could not create process group\n");
			exit(EXIT_FAILURE);
		}

		if ((chdir("/")) < 0) {
			syslog(LOG_ERR, "Could not change working directory to /\n");
			exit(EXIT_FAILURE);
		}
	}
	else {
		// open syslog for non-daemon mode
		openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER);
	}

	//Read configuration file
	cfg_opt_t transmitter_opts[] = {
			CFG_STR_LIST("transmitter", "{1}", CFGF_NONE),
			CFG_INT("frequency", 99800, CFGF_NONE),
			CFG_BOOL("stereo", 1, CFGF_NONE),
			CFG_BOOL("rdsenable", 1, CFGF_NONE),
			CFG_BOOL("poweron", 1, CFGF_NONE),
			CFG_INT("txpower", 3, CFGF_NONE),
			CFG_BOOL("gain", 0, CFGF_NONE),
			CFG_INT("volume", 3, CFGF_NONE),
			CFG_STR("rdsid", "", CFGF_NONE),
			CFG_STR("rdstext", "", CFGF_NONE),
			CFG_INT("rdspi", 0x8000, CFGF_NONE),
			CFG_INT("rdspty", 10, CFGF_NONE),
			CFG_INT("i2cmultiplexeraddress", 0x70, CFGF_NONE),
			CFG_INT("i2cmultiplexerport", 0, CFGF_NONE),
			CFG_STR("IOexpanderconfig", "1_Aports", CFGF_NONE),
			CFG_INT("IOexpanderport",0, CFGF_NONE),
			CFG_END()
		};

	cfg_opt_t IOexpander_opts[]= {	CFG_STR_LIST("IOexpander", "{1_Aports}", CFGF_NONE),
			CFG_INT("IOexpanderaddress", 0x20, CFGF_NONE),
			CFG_INT("IOinterruptpin", 17, CFGF_NONE),
			CFG_END()}
		;

	cfg_opt_t opts[] = {
			CFG_INT("i2cbus", 1, CFGF_NONE),
			CFG_BOOL("tcpbindlocal", 1, CFGF_NONE),
			CFG_INT("tcpport", 42516, CFGF_NONE),
			CFG_INT("rdspin", 17, CFGF_NONE),
			CFG_INT("ledpin", 27, CFGF_NONE),
			CFG_SEC("transmitter", transmitter_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_SEC("IOexpander", IOexpander_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_END()
		};

	cfg = cfg_init(opts, CFGF_NONE);
	if (start_daemon) {
		if (cfg_parse(cfg, "/etc/fmberry.conf") == CFG_PARSE_ERROR)
			return 1;
	}
	else {
	if (cfg_parse(cfg, "/home/pi/git/FMBerry/fmberry.conf") == CFG_PARSE_ERROR)
		return 1;
	}	
	//rdsint = cfg_getint(cfg, "rdspin");

	int nfds;
	struct pollfd  polls[MAXIOEXPANDERS+1]; //+1 for the tcp polling

	//open TCP listener socket, will exit() in case of error
	int tcpport = cfg_getint(cfg, "tcpport");
	int lst = ListenTCP(cfg_getint(cfg, "tcpport"));
	polls[0].fd = lst;
	polls[0].events = POLLIN;
	polls[0].revents=0;
	nfds = 1;


	// read IOexpander data from config
	int nr_IOexpanders =  cfg_size(cfg, "IOexpander");
	bzero(&IOexpander, sizeof(IOexpander));
	for (int k=0; k < nr_IOexpanders; k++) {
		cfg_IOexpander 				= cfg_getnsec(cfg, "IOexpander", k);
		strncpy(IOexpander[k].id, cfg_title(cfg_IOexpander), 12);
		IOexpander[k].address 		= cfg_getint (cfg_IOexpander, "IOexpanderaddress");
		IOexpander[k].interruptpin 	= cfg_getint (cfg_IOexpander, "IOinterruptpin");
	}

	// initialize data structure for 'status' command
	nr_transmitters = cfg_size(cfg, "transmitter");
	bzero(&mmr70, sizeof(mmr70));
	for (int j = 0; j < nr_transmitters; j++) {	
		cfg_transmitter 			= cfg_getnsec(cfg, "transmitter", j);
		strncpy(mmr70[j].name, cfg_title(cfg_transmitter), 32);
		mmr70[j].frequency 			= cfg_getint(cfg_transmitter, "frequency");
		mmr70[j].power 				= cfg_getbool(cfg_transmitter, "poweron");
		mmr70[j].txpower 			= cfg_getint(cfg_transmitter, "txpower");
		mmr70[j].mute 				= 0;
		mmr70[j].gain 				= cfg_getbool(cfg_transmitter, "gain");
		mmr70[j].volume 			= cfg_getint(cfg_transmitter, "volume");
		mmr70[j].stereo 			= cfg_getbool(cfg_transmitter, "stereo");
		mmr70[j].rds 				= cfg_getbool(cfg_transmitter, "rdsenable");
		strncpy(mmr70[j].rdsid, cfg_getstr(cfg_transmitter, "rdsid"), 8);
		strncpy(mmr70[j].rdstext, cfg_getstr(cfg_transmitter, "rdstext"), 64);
		mmr70[j].rdspi 				= cfg_getint(cfg_transmitter, "rdspi");
		mmr70[j].rdspty 			= cfg_getint(cfg_transmitter, "rdspty");
		mmr70[j].i2c_mplexaddress 	= cfg_getint(cfg_transmitter, "i2cmultiplexeraddress");
		mmr70[j].i2c_mplexport	 	= cfg_getint(cfg_transmitter, "i2cmultiplexerport");
		strncpy(mmr70[j].IOexpanderconfig, cfg_getstr(cfg_transmitter, "IOexpanderconfig"), 12);
		mmr70[j].IOexpanderindex = -1;
		for (int k =0; k<nr_IOexpanders;k++)
			if (!strcmp(IOexpander[k].id, mmr70[j].IOexpanderconfig)) {
				mmr70[j].IOexpanderindex = k;
				break;
			}
		if (mmr70[j].IOexpanderindex == -1) {
			syslog(LOG_ERR, "Error with matching IOExpander config for transmitter with IOexpander config section. Correct .conf file. \n");
			exit(EXIT_FAILURE);
		}
		mmr70[j].IOexpanderport 	= cfg_getint(cfg_transmitter, "IOexpanderport");
	}
	
	//build up table so that we quickly can lookup which transmitter belongs to which mcp23017 port while handling rds interupts 
	for (int j=0; j<nr_IOexpanders;j++)
		for (int k=0;k<16;k++)
			IOexpander_to_mmr70[j][k] = -1;
	for (int l=0;l<nr_transmitters;l++)
		IOexpander_to_mmr70[mmr70[l].IOexpanderindex][mmr70[l].IOexpanderport]=l;
	
	// build up index for the multiplexers for initialisation of the i2cbus
	// use the index number for each multiplexer also for 
	int mplex_found;
	int mplexindex=-1;
	bzero(&multiplexer, sizeof(multiplexer));
	for (int j = 0; j < nr_transmitters; j++)
	{
		mplex_found = -1;
		for (int k=0; k < MAXMULTIPLEXERS; k++) {
			if (multiplexer[k].address == mmr70[j].i2c_mplexaddress) {	
				mplex_found = k;
				break;
			}
		}
		if (mplex_found != -1)
			mmr70[j].i2c_mplexindex = mplex_found;
		else {
			mplexindex++;
			multiplexer[mplexindex].address=mmr70[j].i2c_mplexaddress;
			mmr70[j].i2c_mplexindex = mplexindex;
		}
	}



	// initialize the ns741 register map for all transmitters and initialise rds_ps & rds_text registers
	ns741_init_registers(nr_transmitters);

	// initialize all tca9548a multiplexer i2c busses
	if (tca9548a_init_i2c(cfg_getint(cfg, "i2cbus"))==-1) {
		syslog(LOG_ERR, "Init of TCA9548A multiplexer(s) failed! Double-check hardware and .conf and try again!\n");
		syslog(LOG_ERR, "And check if user is either root or has i2c read/write rights, verify with i2cdetect -y 1\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized i2c bus for TCA9548A multiplexer(s).\n");

	// init I2C bus for the mcp23017 IC expander(s)
	if (mcp23017_init_i2c(cfg_getint(cfg, "i2cbus")) == -1 || mcp23017_init_INT(nr_transmitters)==-1) {
		syslog(LOG_ERR, "Init of mcp23017 IO expander(s) failed! Double-check hardware and .conf and try again!\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized i2c bus for mcp23017 IO expander(s)\n");

	// init I2C bus and transmitters with initial frequency and state
	if (ns741_init_i2c(cfg_getint(cfg, "i2cbus"), nr_transmitters) == -1) {
		syslog(LOG_ERR, "Init of transmitters failed! Double-check hardware and .conf and try again!\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized ns741 transmitters.\n");


	// apply configuration parameters
	for (int j = 0; j< nr_transmitters; j++) {
		// enable the transmitter i2c port on the tca9548a 
		tca9548a_select_port(mmr70[j].i2c_mplexindex, mmr70[j].i2c_mplexport);
		// and set the parameters on the transmitter
		ns741_set_frequency (j, mmr70[j].frequency);
		ns741_txpwr(j, mmr70[j].txpower);
		ns741_mute(j, mmr70[j].mute);
		ns741_stereo(j, mmr70[j].stereo);
		ns741_init_rds_registers(j);
		ns741_rds_set_progname(j, mmr70[j].rdsid);
		ns741_rds_set_radiotext(j, mmr70[j].rdstext);
		ns741_rds_set_rds_pi(j, mmr70[j].rdspi);
		ns741_rds_set_rds_pty(j, mmr70[j].rdspty);
		ns741_power(j, mmr70[j].power);
		ns741_input_gain(j, mmr70[j].gain);
		ns741_volume(j, mmr70[j].volume);
	}

	// ns741_rds_debug(1);
	// Use RPI_REV1 for earlier versions of Raspberry Pi
	rpi_pin_init(RPI_REVISION);

	int rds;
	int rdsint;
	for (int j=0;j < nr_IOexpanders;j++) {
		// Get file descriptor for RDS handler
		polls[j+1].revents = 0;
		rdsint=IOexpander[j].interruptpin;
		rds = rpi_pin_poll_enable(rdsint, EDGE_FALLING);
	    if (rds < 0) {
	        printf("Couldn't enable RDS support\n");
			syslog(LOG_ERR, "Could not enable RDS support\n");
	        run = 0;
			break;
	    }
		polls[j+1].fd = rds;
		polls[j+1].events = POLLPRI;
		nfds = 1+nr_IOexpanders;
	}

	// send first two bytes for each transmitter	
	for (int k=0;k<nr_transmitters;k++) {
		if (tca9548a_select_port(mmr70[k].i2c_mplexindex,mmr70[k].i2c_mplexport) == -1) {
			syslog(LOG_ERR, "Could not switch tca9548a\n");
			exit(EXIT_FAILURE);
		}
		ns741_rds(k, mmr70[k].rds);
		ns741_rds_isr(k); 
	}
	

	// main polling loop
	uint16_t trs_rdsstatus;
	uint8_t transmitter;
	int ledcounter = 0;
	int intr_notfinished=99;
	while(run) {
		
		// alternative loop to run without interrupts, however this fully saturates the i2c bus
		// for (int j = 0; j<nr_IOexpanders;j++)
		// {
		// 	trs_rdsstatus = ~mcp23017_read_trs_rdsstatus(j); 
		// 	trs_rdsstatus &= IOexpander[j].GPINTEN;
		// 	for (int k=0;k<16;k++)
		// 	{
		// 		if (trs_rdsstatus & 0x0001)
		// 		{
		// 			transmitter = IOexpander_to_mmr70[j][k];
		// 			if (tca9548a_select_port (mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport) == -1)
		// 			{
		// 				syslog(LOG_ERR, "Port switch tca9548a failed during RDS update\n");
		// 				exit(EXIT_FAILURE);	
		// 			}
		// 			else
		// 			{
		// 				ns741_rds_isr(transmitter);
		// 			}
		// 		}
		// 		trs_rdsstatus >>= 1;
		// 	}
		// }
			
		// if new interrupts from transmitter came during the previous processing, first handle these
		// if all done then wait for the next interrupt
		// note that the interruptpin of the mcp20317 will only return to high if there are no more MRR70's with the interrupt low AND the GPIO register of the MCP23017 is read
		for (int j = 0; j<nr_IOexpanders;j++)
			if (IOexpander[j].intr_notfinished) {
				intr_notfinished = 1;
				break;
			}
		if (!intr_notfinished) 
			if (poll(polls, nfds, 25) < 0)
				break;

		for (int j=0;j<nr_IOexpanders;j++)
		{
			if (polls[j+1].revents || IOexpander[j].intr_notfinished || intr_notfinished == 99) {
				trs_rdsstatus = ~mcp23017_read_trs_rdsstatus(j); 
				trs_rdsstatus &= IOexpander[j].GPINTEN;
				for (int k=0;k<16;k++) {
					if (trs_rdsstatus & 0x0001) {
						transmitter = IOexpander_to_mmr70[j][k];
						tca9548a_select_port (mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
						ns741_rds_isr(transmitter);
					}
					trs_rdsstatus >>= 1;
				}
				//while processing this interrupt, new transmitters might have send an interrupt, remember this for the next run. This also clears the intrrupt pin
				intr_notfinished = 0;
				IOexpander[j].intr_notfinished = (rpi_pin_get(IOexpander[j].interruptpin) == 0) ? 1 : 0;
			}
		}
		if (polls[0].revents)
			 ProcessTCP(lst);
	}

	//clean up at exit
	for (int j=0;j<nr_transmitters;j++)
		ns741_power(transmitter, 0);
	
	for (int j=0;j<MAXIOEXPANDERS;j++)
		if (IOexpander[j].interruptpin) {
			rpi_pin_unexport(IOexpander[j].interruptpin);
			break;
		}

	close(lst);
	closelog();

	return 0;
}


int ListenTCP(uint16_t port)
{
	// Socket erstellen - TCP, IPv4, keine Optionen 
	int lsd = socket(AF_INET, SOCK_STREAM, 0);

	// IPv4, Port: 1111, jede IP-Adresse akzeptieren 
	struct sockaddr_in saddr;
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	if (cfg_getbool(cfg, "tcpbindlocal"))
	{
		syslog(LOG_NOTICE, "Binding to localhost.\n");
    	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
	else
	{
		syslog(LOG_NOTICE, "Binding to any interface.\n");
    	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

	//Important! Makes sure you can restart the daemon without any problems!
	const int       optVal = 1;
	const socklen_t optLen = sizeof(optVal);

	int rtn = setsockopt(lsd, SOL_SOCKET, SO_REUSEADDR, (void*) &optVal, optLen);

	//assert(rtn == 0);   // this is optional 

	// Socket an Port binden 
	if (bind(lsd, (struct sockaddr*) &saddr, sizeof(saddr)) < 0) {
  		//whoops. Could not listen
  		syslog(LOG_ERR, "Could not bind to TCP port! Terminated.\n");
  		exit(EXIT_FAILURE);
	 }

  	syslog(LOG_NOTICE, "Successfully started daemon\n");
	// Auf Socket horchen (Listen) 
	listen(lsd, 10);

	return lsd;
}

// for 'status' command
static float txpower[4] = { 0.5, 0.8, 1.0, 2.0 };

// int ProcessTCP(int sock, mmr70_data_t *pdata)
int ProcessTCP(int sock)
{
	// Puffer und Strukturen anlegen
	struct sockaddr_in clientaddr;
	socklen_t clen = sizeof(clientaddr);
	char arg_buffer[512];
	bzero(arg_buffer, sizeof(arg_buffer));
	char full_buffer[512];
	bzero(full_buffer, sizeof(full_buffer));

	// Auf Verbindung warten, bei Verbindung Connected-Socket erstellen 
	int csd = accept(sock, (struct sockaddr *)&clientaddr, &clen);

	struct pollfd  pol;
	pol.fd = csd;
	pol.events = POLLRDNORM;

	// just to be on a safe side check if data is available
	if (poll(&pol, 1, 1000) <= 0) {
		close(csd);
		return -1;
	}

	int len  = recv(csd, full_buffer, sizeof(full_buffer) - 2, 0);
	full_buffer[len] = '\0';
	char *end = full_buffer + len - 1;
	// remove any trailing spaces
	while((end != full_buffer) && (*end <= ' ')) {
		*end-- = '\0';
	}

	// read for which transmitter(s) the command needs to be executed
	// format ctlberry transmitter1,transmitter2,...|all action
	int do_fortransmitter[MAXTRANSMITTERS]; 
	int transmittersspecified = 0;
	bzero(do_fortransmitter, sizeof(do_fortransmitter));
	if (strncmp(full_buffer, "all", 3) == 0) {
		for (int j = 0; j<nr_transmitters;j++) {
			do_fortransmitter[j]=1;
			transmittersspecified++;
		}
	}
	else {
		int f=0; int t=0;
		char transmittername[32];
		bzero(transmittername, sizeof(transmittername));
		do {
			if (full_buffer[f] != ',' && full_buffer[f] != ' ')
				transmittername[t++]=full_buffer[f];
			
			if (t) {
				for (int k=0; k<nr_transmitters;k++) {
					if (strcmp(mmr70[k].name, transmittername) == 0) {
						do_fortransmitter[k]=1;
						transmittersspecified++;
						bzero(transmittername, sizeof(transmittername));
						t=0;
						break;
					}
				}
			}
		f++;
		if (full_buffer[f] == ' ' || full_buffer[f] == 0)
				break;
		} while (1);
	} 

	if (!transmittersspecified) {
		syslog(LOG_NOTICE, "Transmitter(s) missing. Use: cltfmberry transmitter1[,transmitter..]|all <command>\n");
		close(csd);
		return -1;
	}

	int i=0;
	while (full_buffer[i++] != ' ');
	strncpy(arg_buffer, full_buffer+i, sizeof(arg_buffer)-i);

	for (int transmitter=0;transmitter<nr_transmitters;transmitter++) {
		do {
			const char *arg;

			if (do_fortransmitter[transmitter]) {

				if (str_is_arg(arg_buffer, "set freq", &arg))
				{
					int frequency = atoi(arg);
					
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					if ((frequency >= 76000) && (frequency <= 108000))
					{
						syslog(LOG_NOTICE, "Changing frequency...\n");
						ns741_set_frequency(transmitter,frequency);
						mmr70[transmitter].frequency = frequency;
					}
					else
					{
						syslog(LOG_NOTICE, "Bad frequency.\n");
					}
					break;
				}

				if (str_is(arg_buffer, "poweroff"))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					ns741_power(transmitter,0);
					mmr70[transmitter].power = 0;
					break;
				}

				if (str_is(arg_buffer, "poweron"))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					ns741_power(transmitter,1);
					ns741_rds(transmitter,1);
					ns741_rds_reset_radiotext(transmitter);
					mmr70[transmitter].power = 1;
					break;
				}

				if (str_is(arg_buffer, "muteon"))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					ns741_mute(transmitter,1);
					mmr70[transmitter].mute = 1;
					break;
				}

				if (str_is(arg_buffer, "muteoff"))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					ns741_mute(transmitter, 0);
					mmr70[transmitter].mute = 0;
					break;
				}

				if (str_is(arg_buffer, "gainlow"))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					ns741_input_gain(transmitter,1);
					mmr70[transmitter].gain = 1;
					break;
				}

				if (str_is(arg_buffer, "gainoff"))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					ns741_input_gain(transmitter,0);
					mmr70[transmitter].gain = 0;
					break;
				}

				if (str_is_arg(arg_buffer, "set volume", &arg))
				{
					int volume = atoi(arg);

					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					if ((volume >= 0) && (volume <= 6))
					{
						syslog(LOG_NOTICE, "Changing volume level...\n");
						ns741_volume(transmitter,volume);
						mmr70[transmitter].volume = volume;
					}
					else
					{
						syslog(LOG_NOTICE, "Bad volume level. Range 0-6\n");
					}
					break;
				}

				if (str_is_arg(arg_buffer, "set stereo", &arg))
				{
					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					if (str_is(arg, "on"))
					{
						syslog(LOG_NOTICE, "Enabling stereo signal...\n");
						ns741_stereo(transmitter, 1);
						mmr70[transmitter].stereo = 1;
						break;
					}
					if (str_is(arg, "off"))
					{
						syslog(LOG_NOTICE, "Disabling stereo signal...\n");
						ns741_stereo(transmitter, 0);
						mmr70[transmitter].stereo = 0;
					}
					break;
				}

				if (str_is_arg(arg_buffer, "set txpwr", &arg))
				{
					int txpwr = atoi(arg);

					tca9548a_select_port(mmr70[transmitter].i2c_mplexindex, mmr70[transmitter].i2c_mplexport);
					if ((txpwr >= 0) && (txpwr <= 3))
					{
						syslog(LOG_NOTICE, "Changing transmit power...\n");
						ns741_txpwr(transmitter, txpwr);
						mmr70[transmitter].txpower = txpwr;
					}
					else
					{
						syslog(LOG_NOTICE, "Bad transmit power. Range 0-3\n");
					}
					break;
				}

				if (str_is_arg(arg_buffer, "set rdstext", &arg))
				{
					strncpy (mmr70[transmitter].rdstext, arg, 64);
					ns741_rds_set_radiotext (transmitter, mmr70[transmitter].rdstext);
					break;
				}

				if (str_is_arg(arg_buffer, "set rdsid", &arg))
				{
					bzero (mmr70[transmitter].rdsid, sizeof(mmr70[transmitter].rdsid));
					strncpy (mmr70[transmitter].rdsid, arg, 8);
					ns741_rds_set_progname(transmitter, mmr70[transmitter].rdsid);
					ns741_rds_reset_radiotext(transmitter);
					break;
				}

				if (str_is_arg(arg_buffer, "set rdspi", &arg))
				{
					int rds_pi = strtol(arg, NULL, 16);
					if ((rds_pi >= 0x0000) && (rds_pi <= 0xFFFF))
					{
						ns741_rds_set_rds_pi(transmitter, rds_pi);
						mmr70[transmitter].rdspi = rds_pi;
					}
					break;
				}	

				if (str_is_arg(arg_buffer, "set rdspty", &arg))
				{
					int rds_pty = atoi(arg);
					if ((rds_pty >= 0) && (rds_pty < 32))
					{
						ns741_rds_set_rds_pty(transmitter, rds_pty);
						mmr70[transmitter].rdspty = rds_pty;
					}
					break;
				}					
				
				if (str_is(arg_buffer, "die") || str_is(arg_buffer, "stop"))
				{
					run = 0;
					syslog(LOG_NOTICE, "Shutting down.\n");
					break;
				}

				if (str_is_arg(arg_buffer, "rdsdebug on", &arg))
				{
					ns741_rds_debug(1);
					break;
				}

				if (str_is(arg_buffer, "status"))
				{
					char status_buffer[256];
					bzero(status_buffer, sizeof(status_buffer));
					sprintf(status_buffer, "\033[0;36m\x23%d name: %s\033[0;0m freq: %dKHz txpwr: %.2fmW power: '%s' mute: '%s' gain: '%s' volume: '%d' stereo: '%s' rds: '%s' rdspi: 0x%04X rdspty: %d rdsid: '%s' rdstext: '%s'\n",
						transmitter,
						mmr70[transmitter].name,
						mmr70[transmitter].frequency,
						txpower[mmr70[transmitter].txpower],
						mmr70[transmitter].power ? "on" : "off",
						mmr70[transmitter].mute ? "on" : "off",
						mmr70[transmitter].gain ? "on" : "off",
						mmr70[transmitter].volume,
						mmr70[transmitter].stereo ? "on" : "off",
						mmr70[transmitter].rds ? "on" : "off",
						mmr70[transmitter].rdspi,
						mmr70[transmitter].rdspty,
						mmr70[transmitter].rdsid, 
						mmr70[transmitter].rdstext);

					write(csd, status_buffer, strlen(status_buffer) + 1);
					break;
				}

				//still here? command not recognised. Need to get out of the loop
				break;
			}
			else
				break;
		} while(run);
	}

	close(csd);
	return 0;
}

// helper string compare functions
int str_is(const char *str, const char *is)
{
	if (strcmp(str, is) == 0)
		return 1;
	return 0;
}

int str_is_arg(const char *str, const char *is, const char **arg)
{
	size_t len = strlen(is);
	if (strncmp(str, is, len) == 0) {
		str = str + len;
		// remove any leading spaces from the arg
		while(*str && (*str <= ' '))
			str++;
		*arg = str;
		return 1;
	}
	return 0;

}
