/*
	FMBerry - an cheap and easy way of transmitting music with your Pi.
    Copyright (C) 2011-2013 by Tobias Mädel (t.maedel@alfeld.de)
	Copyright (C) 2013      by Andrey Chilikin (https://github.com/achilikin)
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


// RDS interrupt pin
int rdsint = 17;

// LED pin number
int ledpin = -1;

mmr70_data_t mmr70[MAXTRANSMITTERS];
IOexpander_data_t IOexpander[MAXIOEXPANDERS];
multiplexer_data_t multiplexer[MAXMULTIPLEXERS];

static cfg_t *cfg, *cfg_transmitter, *cfg_IOexpander;
static volatile int run = 1;
static int start_daemon = 1;

int main(int argc, char **argv)
{
	//Check if user == root
/*	if(geteuid() != 0)
	{
	  puts("Please run this software as root!");
	  exit(EXIT_FAILURE);
	}
*/
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
		if (pid < 0)
		{
			exit(EXIT_FAILURE);
		}
		if (pid > 0)
		{
			exit(EXIT_SUCCESS);
		}

		umask(0);

		//We are now running as the forked child process.
		openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER);

		pid_t sid;
		sid = setsid();
		if (sid < 0)
		{
			syslog(LOG_ERR, "Could not create process group\n");
			exit(EXIT_FAILURE);
		}

		if ((chdir("/")) < 0)
		{
			syslog(LOG_ERR, "Could not change working directory to /\n");
			exit(EXIT_FAILURE);
		}
	}
	else {
		// open syslog for non-daemon mode
		openlog(argv[0],LOG_NOWAIT|LOG_PID,LOG_USER);
	}

//Read configuration file
	cfg_opt_t transmitter_opts[] =
		{
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
			CFG_END()};

	cfg_opt_t IOexpander_opts[]=
		{	CFG_STR_LIST("IOexpander", "{1_Aports}", CFGF_NONE),
			CFG_INT("IOexpanderaddress", 0x20, CFGF_NONE),
			CFG_INT("IOinterruptpin", 17, CFGF_NONE),
			CFG_END()};

	cfg_opt_t opts[] =
		{
			CFG_INT("i2cbus", 1, CFGF_NONE),
			CFG_BOOL("tcpbindlocal", 1, CFGF_NONE),
			CFG_INT("tcpport", 42516, CFGF_NONE),
			CFG_INT("rdspin", 17, CFGF_NONE),
			CFG_INT("ledpin", 27, CFGF_NONE),
			CFG_SEC("transmitter", transmitter_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_SEC("IOexpander", IOexpander_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_END()};

	cfg = cfg_init(opts, CFGF_NONE);
	if (cfg_parse(cfg, "/home/pi/git/FMBerry/fmberry.conf") == CFG_PARSE_ERROR)
		return 1;

	// get LED pin number
	int led = 1; // led state
	ledpin = cfg_getint(cfg, "ledpin");
	rdsint = cfg_getint(cfg, "rdspin");

	int nfds;
	struct pollfd  polls[MAXIOEXPANDERS+1]; //+1 for the tcp polling
/*
	// open TCP listener socket, will exit() in case of error
	int lst = ListenTCP(cfg_getint(cfg, "tcpport"));
	polls[0].fd = lst;
	polls[0].events = POLLIN;
	nfds = 1;
*/

	// read IOexpander data from config
	int nr_IOexpanders =  cfg_size(cfg, "IOexpander");
	bzero(&IOexpander, sizeof(IOexpander));
	for (int k=0; k < nr_IOexpanders; k++)
	{
		cfg_IOexpander 				= cfg_getnsec(cfg, "IOexpander", k);
		strncpy(IOexpander[k].id, cfg_title(cfg_IOexpander), 12);
		IOexpander[k].address 		= cfg_getint (cfg_IOexpander, "IOexpanderaddress");
		IOexpander[k].interruptpin 	= cfg_getint (cfg_IOexpander, "IOinterruptpin");
	}

	// initialize data structure for 'status' command
	int nr_transmitters = cfg_size(cfg, "transmitter");
	bzero(&mmr70, sizeof(mmr70));
	for (int j = 0; j < nr_transmitters; j++)
	{	cfg_transmitter 			= cfg_getnsec(cfg, "transmitter", j);
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
			if (!strcmp(IOexpander[k].id, mmr70[j].IOexpanderconfig))
			{
				mmr70[j].IOexpanderindex = k;
				break;
			}
		if (mmr70[j].IOexpanderindex == -1)
		{
			syslog(LOG_ERR, "Error with matching IOExpander config for transmitter with IOexpander config section. Correct .conf file. \n");
			exit(EXIT_FAILURE);
		}
		mmr70[j].IOexpanderport 	= cfg_getint(cfg_transmitter, "IOexpanderport");
	}
	
	// build up index for the multiplexers for initialisation of the i2cbus
	// use the index number for each multiplexer also for 
	int mplex_found;
	int mplexindex=-1;
	bzero(&multiplexer, sizeof(multiplexer));
	for (int j = 0; j < nr_transmitters; j++)
	{
		mplex_found = -1;
		for (int k=0; k < MAXMULTIPLEXERS; k++)
		{
			if (multiplexer[k].address == mmr70[j].i2c_mplexaddress)
			{	
				mplex_found = k;
				break;
			}
		}
		if (mplex_found != -1)
			mmr70[j].i2c_mplexindex = mplex_found;
		else
		{
			mplexindex++;
			multiplexer[mplexindex].address=mmr70[j].i2c_mplexaddress;
			mmr70[j].i2c_mplexindex = mplexindex;
		}
	}

	// initialize the ns741 register map for all transmitters
	ns741_init_reg(nr_transmitters);

	// initialize all tca9548a multiplexer i2c busses
	if (tca9548a_init_i2c(cfg_getint(cfg, "i2cbus"))==-1)
	{
		syslog(LOG_ERR, "Init of TCA9548A multiplexer(s) failed! Double-check hardware and .conf and try again!\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized i2c bus for TCA9548A multiplexer(s).\n");

	// init I2C bus for the mcp23017 IC expander(s)
	if (mcp23017_init_i2c(cfg_getint(cfg, "i2cbus")) == -1 || mcp23017_init_INT()==-1)
	{
		syslog(LOG_ERR, "Init of mcp23017 IO expander(s) failed! Double-check hardware and .conf and try again!\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized i2c bus for mcp23017 IO expander(s)\n");

	// init I2C bus and transmitters with initial frequency and state
	if (ns741_init_i2c(cfg_getint(cfg, "i2cbus"), nr_transmitters) == -1)
	{
		syslog(LOG_ERR, "Init of transmitters failed! Double-check hardware and .conf and try again!\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_NOTICE, "Successfully initialized ns741 transmitters.\n");


	// apply configuration parameters
	for (int j = 0; j< nr_transmitters; j++)
	{
		// enable the transmitter i2c port on the tca9548a 
		tca9548a_select_port(mmr70[j].i2c_mplexindex, mmr70[j].i2c_mplexport);
		// and set the parameters on the transmitter
		ns741_set_frequency (j, mmr70[j].frequency);
		ns741_txpwr(j, mmr70[j].txpower);
		ns741_mute(j, mmr70[j].mute);
		ns741_stereo(j, mmr70[j].stereo);
		ns741_rds_set_progname(j, mmr70[j].rdsid);
		ns741_rds_set_radiotext(j, mmr70[j].rdstext);
		ns741_power(j, mmr70[j].power);
		ns741_input_gain(j, mmr70[j].gain);
		ns741_volume(j, mmr70[j].volume);
		// nog maken functies voor rdspi en rdspty
	}

	int xy = mcp23017_read_trs_rdsstatus(0); 
	// Use RPI_REV1 for earlier versions of Raspberry Pi
	rpi_pin_init(RPI_REVISION);

	int rds;
	for (int j=1;j < nr_IOexpanders+1;j++)
	{
		// Get file descriptor for RDS handler
		polls[j].revents = 0;
		rdsint=IOexpander[j].interruptpin;
		rds = rpi_pin_poll_enable(rdsint, EDGE_FALLING);
	    if (rds < 0) {
	        printf("Couldn't enable RDS support\n");
	        run = 0;
			break;
	    }
		polls[j].fd = rds;
		polls[j].events = POLLPRI;
		nfds = 2;

		for (int k=0;k<nr_transmitters;k++)
		{
			ns741_rds(k, 1);
			ns741_rds_isr(k); // send first two bytes
		}
	}
/*
	// main polling loop
	int ledcounter = 0;
	while(run) {
		if (poll(polls, nfds, -1) < 0)
			break;

		if (polls[1].revents) {
			rpi_pin_poll_clear(polls[1].fd);
			ns741_rds_isr();
			// flash LED if enabled on every other RDS refresh cycle
			if (ledpin > 0) {
				ledcounter++;
				if (!(ledcounter % 80)) {
					led ^= 1;
					rpi_pin_set(ledpin, led);
				}
			}
		}

		if (polls[0].revents)
			ProcessTCP(lst, &mmr70);
	}

	// clean up at exit
	ns741_power(0);
	if (mmr70.rds)
		rpi_pin_unexport(rdsint);

	if (ledpin > 0) {
		rpi_pin_set(ledpin, 0);
		rpi_pin_unexport(ledpin);
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

int ProcessTCP(int sock, mmr70_data_t *pdata)
{
	// Puffer und Strukturen anlegen
	struct sockaddr_in clientaddr;
	socklen_t clen = sizeof(clientaddr);
	char buffer[512];
	bzero(buffer, sizeof(buffer));

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

	int len  = recv(csd, buffer, sizeof(buffer) - 2, 0);
	buffer[len] = '\0';
	char *end = buffer + len - 1;
	// remove any trailing spaces
	while((end != buffer) && (*end <= ' ')) {
		*end-- = '\0';
	}

	do {
		const char *arg;

		if (str_is_arg(buffer, "set freq", &arg))
		{
			int frequency = atoi(arg);

			if ((frequency >= 76000) && (frequency <= 108000))
			{
				syslog(LOG_NOTICE, "Changing frequency...\n");
				ns741_set_frequency(frequency);
				pdata->frequency = frequency;
			}
			else
			{
				syslog(LOG_NOTICE, "Bad frequency.\n");
			}
			break;
		}

		if (str_is(buffer, "poweroff"))
		{
			ns741_power(0);
			pdata->power = 0;
			break;
		}

		if (str_is(buffer, "poweron"))
		{
			ns741_power(1);
			ns741_rds(1);
			ns741_rds_reset_radiotext();
			pdata->power = 1;
			break;
		}

		if (str_is(buffer, "muteon"))
		{
			ns741_mute(1);
			pdata->mute = 1;
			break;
		}

		if (str_is(buffer, "muteoff"))
		{
			ns741_mute(0);
			pdata->mute = 0;
			break;
		}

		if (str_is(buffer, "gainlow"))
		{
			ns741_input_gain(1);
			pdata->gain = 1;
			break;
		}

		if (str_is(buffer, "gainoff"))
		{
			ns741_input_gain(0);
			pdata->gain = 0;
			break;
		}

		if (str_is_arg(buffer, "set volume", &arg))
		{
			int volume = atoi(arg);

			if ((volume >= 0) && (volume <= 6))
			{
				syslog(LOG_NOTICE, "Changing volume level...\n");
				ns741_volume(volume);
				pdata->volume = volume;
			}
			else
			{
				syslog(LOG_NOTICE, "Bad volume level. Range 0-6\n");
			}
			break;
		}

		if (str_is_arg(buffer, "set stereo", &arg))
		{
			if (str_is(arg, "on"))
			{
				syslog(LOG_NOTICE, "Enabling stereo signal...\n");
				ns741_stereo(1);
				pdata->stereo = 1;
				break;
			}
			if (str_is(arg, "off"))
			{
				syslog(LOG_NOTICE, "Disabling stereo signal...\n");
				ns741_stereo(0);
				pdata->stereo = 0;
			}
			break;
		}

		if (str_is_arg(buffer, "set txpwr", &arg))
		{
			int txpwr = atoi(arg);

			if ((txpwr >= 0) && (txpwr <= 3))
			{
				syslog(LOG_NOTICE, "Changing transmit power...\n");
				ns741_txpwr(txpwr);
				pdata->txpower = txpwr;
			}
			else
			{
				syslog(LOG_NOTICE, "Bad transmit power. Range 0-3\n");
			}
			break;
		}

		if (str_is_arg(buffer, "set rdstext", &arg))
		{
			strncpy(pdata->rdstext, arg, 64);
			ns741_rds_set_radiotext(pdata->rdstext);
			break;
		}

		if (str_is_arg(buffer, "set rdsid", &arg))
		{
			bzero(pdata->rdsid, sizeof(pdata->rdsid));
			strncpy(pdata->rdsid, arg, 8);
			// ns741_rds_set_progname() will pad rdsid with spaces if needed
			ns741_rds_set_progname(pdata->rdsid);
			ns741_rds_reset_radiotext();
			break;
		}

		if (str_is(buffer, "die") || str_is(buffer, "stop"))
		{
			run = 0;
			syslog(LOG_NOTICE, "Shutting down.\n");
			break;
		}

		if (str_is(buffer, "status"))
		{
			bzero(buffer, sizeof(buffer));
			sprintf(buffer, "freq: %dKHz txpwr: %.2fmW power: '%s' mute: '%s' gain: '%s' volume: '%d' stereo: '%s' rds: '%s' rdsid: '%s' rdstext: '%s'\n",
				pdata->frequency,
				txpower[pdata->txpower],
				pdata->power ? "on" : "off",
				pdata->mute ? "on" : "off",
				pdata->gain ? "on" : "off",
				pdata->volume,
				pdata->stereo ? "on" : "off",
				pdata->rds ? "on" : "off",
				pdata->rdsid, pdata->rdstext);
			write(csd, buffer, strlen(buffer) + 1);
			break;
		}

	} while(0);

	close(csd);
*/	return 0;
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
