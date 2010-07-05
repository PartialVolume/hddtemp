/*
 * Copyright (C) 2002  Emmanuel VARAGNAT <hddtemp@guzu.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * Added some SCSI support: Frederic LOCHON <lochon@roulaise.net>
 *
 */

// Include file generated by ./configure
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// Gettext includes
#if ENABLE_NLS
#include <libintl.h>
#define _(String) gettext (String)
#else
#define _(String) (String)
#endif

// Standard includes
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <locale.h>
#include <time.h>
#include <netinet/in.h>
#include <linux/hdreg.h>
#include <ctype.h>
#include <assert.h>

// Application specific includes
#include "ata.h"
#include "utf8.h"
#include "sata.h"
#include "scsi.h"
#include "db.h"
#include "hddtemp.h"
#include "backtrace.h"
#include "daemon.h"


#define PORT_NUMBER            7634
#define SEPARATOR              '|'

char *             database_path = DEFAULT_DATABASE_PATH;
long               portnum, syslog_interval;
char *             listen_addr;
char               separator = SEPARATOR;

struct bustype *   bus[BUS_TYPE_MAX];
int                tcp_daemon, debug, quiet, numeric, wakeup, af_hint, celsius;

/*******************************************************
 *******************************************************/

static void init_bus_types() {
  bus[BUS_SATA] = &sata_bus;
  bus[BUS_ATA] = &ata_bus;
  bus[BUS_SCSI] = &scsi_bus;
}

/*******************************************************
 *******************************************************/

void value_to_unit(struct disk *dsk) {
  if(celsius) {
    if(dsk->db_entry->unit == 'F')
      dsk->value = F_to_C(dsk->value);
  }
  else {
    if(dsk->db_entry->unit == 'C')
      dsk->value = C_to_F(dsk->value);
  }
}


static enum e_bustype probe_bus_type(struct disk *dsk) {
  /* SATA disks answer to both ATA and SCSI commands so
     they have to be probed first in order to be detected */
  if(bus[BUS_SATA]->probe(dsk->fd))
    return BUS_SATA;
  else if(bus[BUS_ATA]->probe(dsk->fd))
    return BUS_ATA;	  
  else if(bus[BUS_SCSI]->probe(dsk->fd))
    return BUS_SCSI;
  else
    return BUS_UNKNOWN;
}

/*
static int get_smart_threshold_values(int fd, unsigned char* buff) {
  unsigned char cmd[516] = { WIN_SMART, 0, SMART_READ_THRESHOLDS, 1 };
  int ret;

  ret = ioctl(fd, HDIO_DRIVE_CMD, cmd);
  if(ret)
    return ret;

  memcpy(buff, cmd+4, 512);
  return 0;
}
*/


static void display_temperature(struct disk *dsk) {
  enum e_gettemp ret;
  char *degree;

  if(dsk->type != ERROR && debug ) {
    printf(_("\n================= hddtemp %s ==================\n"
	     "Model: %s\n\n"), VERSION, dsk->model);
    /*    return;*/
  }

  if(dsk->type == ERROR
     || bus[dsk->type]->get_temperature == NULL
     || (ret = bus[dsk->type]->get_temperature(dsk)) == GETTEMP_ERROR )
  {
    fprintf(stderr, "%s: %s\n", dsk->drive, dsk->errormsg);
    return;
  }

  if(debug)
    return;

  degree = degree_sign();
  switch(ret) {
  case GETTEMP_ERROR:
    /* see above */
    break;
  case GETTEMP_NOT_APPLICABLE:
    printf("%s: %s: %s\n", dsk->drive, dsk->model, dsk->errormsg);    
    break;
  case GETTEMP_UNKNOWN:
    if(!quiet)
      fprintf(stderr,
	      _("WARNING: Drive %s doesn't seem to have a temperature sensor.\n"
		"WARNING: This doesn't mean it hasn't got one.\n"
		"WARNING: If you are sure it has one, please contact me (hddtemp@guzu.net).\n"
		"WARNING: See --help, --debug and --drivebase options.\n"), dsk->drive);
    printf(_("%s: %s:  no sensor\n"), dsk->drive, dsk->model);
    break;
  case GETTEMP_GUESS:
    if(!quiet)
      fprintf(stderr,
	      _("WARNING: Drive %s doesn't appear in the database of supported drives\n"
		"WARNING: But using a common value, it reports something.\n"
		"WARNING: Note that the temperature shown could be wrong.\n"
		"WARNING: See --help, --debug and --drivebase options.\n"
		"WARNING: And don't forget you can add your drive to hddtemp.db\n"), dsk->drive);
    printf(_("%s: %s:  %d%sC or %sF\n"), dsk->drive, dsk->model, dsk->value, degree, degree);
    break;
  case GETTEMP_KNOWN:

    value_to_unit(dsk);

    if (! numeric)
       printf("%s: %s: %d%sC\n",
              dsk->drive,
              dsk->model,
	      dsk->value,
              degree);
    else
       printf("%d\n", dsk->value);
    break;
  case GETTEMP_DRIVE_SLEEP:
    printf(_("%s: %s: drive is sleeping\n"), dsk->drive, dsk->model);    
    break;
  case GETTEMP_NOSENSOR:
    printf(_("%s: %s:  drive supported, but it doesn't have a temperature sensor.\n"), dsk->drive, dsk->model);
    break;
  default:
    fprintf(stderr, _("ERROR: %s: %s: unknown returned status\n"), dsk->drive, dsk->model);
    break;
  }
  free(degree);
}


void do_direct_mode(struct disk *ldisks) {
  struct disk *dsk;

  for(dsk = ldisks; dsk; dsk = dsk->next) {
    display_temperature(dsk);
  }
  
  if(debug) {
    printf(_("\n"
	     "If one of the field value seems to match the temperature, be sure to read\n"
	     "the hddtemp man page before sending a report (section REPORT). Thanks.\n"
	     ));
  }
}


int main(int argc, char* argv[]) {
  int           i, c, lindex = 0, db_loaded = 0;
  int           show_db;
  struct disk * ldisks;

  backtrace_sigsegv();
  backtrace_sigill();
  backtrace_sigbus();

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);
  
  show_db = debug = numeric = quiet = wakeup = af_hint = syslog_interval = 0;
  celsius = 1;
  portnum = PORT_NUMBER;
  listen_addr = NULL;

  /* Parse command line */
  optind = 0;
  while (1) {
    static struct option long_options[] = {
      {"help",       0, NULL, 'h'},
      {"quiet",      0, NULL, 'q'},
      {"daemon",     0, NULL, 'd'},
      {"drivebase",  0, NULL, 'b'},
      {"debug",      0, NULL, 'D'},
      {"file",       1, NULL, 'f'},
      {"fahrenheit", 0, NULL, 'F'},
      {"listen",     1, NULL, 'l'},
      {"version",    0, NULL, 'v'},
      {"port",       1, NULL, 'p'},
      {"separator",  1, NULL, 's'},
      {"numeric",    0, NULL, 'n'},
      {"syslog",     1, NULL, 'S'},
      {"wake-up",    0, NULL, 'w'},
      {0, 0, 0, 0}
    };
 
    c = getopt_long (argc, argv, "bDFdf:l:hp:qs:vnw46S:", long_options, &lindex);
    if (c == -1)
      break;
    
    switch (c) {
      case 'q':
	quiet = 1;
	break;
      case '4':
	af_hint = AF_INET;
	break;
      case '6':
	af_hint = AF_INET6;
	break;
      case 'b':
	show_db = 1;
	break;
      case 'd':
	tcp_daemon = 1;
	break;
      case 'F':
	celsius = 0;
	break;
      case 'D':
	debug = 1;
	break;
      case 'f':
	database_path = optarg;
	break;
      case 's':
	separator = optarg[0];
	if(separator == '\0') {
	  fprintf(stderr, _("ERROR: invalid separator.\n"));
	  exit(1);
	}
	break;
      case 'p':
	{
	  char *end = NULL;

	  portnum = strtol(optarg, &end, 10);

	  if(errno == ERANGE || end == optarg || *end != '\0' || portnum < 1) {
	    fprintf(stderr, _("ERROR: invalid port number.\n"));
	    exit(1);
	  }
	}
	break;
      case 'l':
	listen_addr = optarg;
	break;
      case '?':
      case 'h':
	printf(_(" Usage: hddtemp [OPTIONS] [TYPE:]DISK1 [[TYPE:]DISK2]...\n"
		 "\n"
		 "   hddtemp displays the temperature of drives supplied in argument.\n"
		 "   Drives must support S.M.A.R.T.\n"
		 "\n"
		 "  TYPE could be SATA, PATA or SCSI. If omitted hddtemp will try to guess.\n"
		 "\n"
		 "  -b   --drivebase   :  display database file content that allow hddtemp to\n"
		 "                        recognize supported drives.\n"
		 "  -D   --debug       :  display various S.M.A.R.T. fields and their values.\n"
		 "                        Useful to find a value that seems to match the\n"
		 "                        temperature and/or to send me a report.\n"
		 "                        (done for every drive supplied).\n"
		 "  -d   --daemon      :  run hddtemp in TCP/IP daemon mode (port %d by default.)\n"
		 "  -f   --file=FILE   :  specify database file to use.\n"
		 "  -l   --listen=addr :  listen on a specific interface (in TCP/IP daemon mode).\n"
                 "  -n   --numeric     :  print only the temperature.\n"
                 "  -F   --fahrenheit  :  output temperature in Fahrenheit.\n"
		 "  -p   --port=#      :  port to listen to (in TCP/IP daemon mode).\n"
		 "  -s   --separator=C :  separator to use between fields (in TCP/IP daemon mode).\n"
		 "  -S   --syslog=s    :  log temperature to syslog every s seconds.\n"
		 "  -q   --quiet       :  do not check if the drive is supported.\n"
		 "  -v   --version     :  display hddtemp version number.\n"
		 "  -w   --wake-up     :  wake-up the drive if need.\n"
		 "  -4                 :  listen on IPv4 sockets only.\n"
		 "  -6                 :  listen on IPv6 sockets only.\n"
		 "\n"
		 "Report bugs or new drives to <hddtemp@guzu.net>.\n"),
	       PORT_NUMBER);
      case 'v':
	printf(_("hddtemp version %s\n"), VERSION);
	exit(0);
	break; 
      case 'n':
        numeric = 1;
        break;
      case 'w':
	wakeup = 1;
	break;
      case 'S':
	{
	  char *end = NULL;

	  syslog_interval = strtol(optarg, &end, 10);

	  if(errno == ERANGE || end == optarg || *end != '\0' || syslog_interval < 1) {
	    fprintf(stderr, _("ERROR: invalid interval.\n"));
	    exit(1);
	  }
        }
	break;
      default:
	exit(1);
      }
  }
  
  if(show_db) {
     load_database(database_path);
     display_supported_drives();
     exit(0);
  }
  
  if(argc - optind <= 0) {
    fprintf(stderr, _("Too few arguments: you must specify one drive, at least.\n"));
    exit(1);
  }

  if(debug) {
    /*    argc = optind + 1;*/
    quiet = 1;
  }

  if(debug && (tcp_daemon || syslog_interval != 0)) {
    fprintf(stderr, _("ERROR: can't use --debug and --daemon or --syslog options together.\n"));
    exit(1);
  }

  init_bus_types();

  /* collect disks informations */
  ldisks = NULL;
  for(i = argc - 1; i >= optind; i--) {
    struct disk *dsk = (struct disk *) malloc(sizeof(struct disk));
    char *p;

    assert(dsk);

    memset(dsk, 0, sizeof(dsk));

    p = strchr(argv[i], ':');
    if(p == NULL)
      dsk->drive = argv[i];
    else {
      char *q;
      int j;

      /* upper case type */
      for(q = argv[i]; q != p; q++)
	*q = (char) toupper(*q);

      /* force bus type */
      for(j = 0; j < BUS_TYPE_MAX; j++) {
	if(bus[j] &&
	   bus[j]->name &&
	   strncmp(bus[j]->name, argv[i], p - argv[i] - 1) == 0)
	  {
	    dsk->type = j;
	    break;
	  }
      }

      dsk->drive = p + 1;      
    }

    dsk->next = ldisks;
    ldisks = dsk;    

    errno = 0;
    dsk->errormsg[0] = '\0';
    if( (dsk->fd = open(dsk->drive, O_RDONLY | O_NONBLOCK)) < 0) {
      snprintf(dsk->errormsg, MAX_ERRORMSG_SIZE, "open: %s\n", strerror(errno));
      dsk->type = ERROR;
      continue;
    } else if( ! dsk->type ) {
      dsk->type = probe_bus_type(dsk);
    }

    if(dsk->type == BUS_UNKNOWN) {
      fprintf(stderr, _("ERROR: %s: can't determine bus type (or this bus type is unknown)\n"), dsk->drive);

      ldisks = dsk->next;
      free(dsk);
      continue;
    }

    dsk->model = bus[dsk->type]->model(dsk->fd);
    dsk->value = -1;
    if(dsk->type != BUS_SCSI) {
      struct harddrive_entry   *dbe;

      if(!db_loaded) {
	load_database(database_path);
	db_loaded = 1;
      }      

      dbe = is_a_supported_drive(dsk->model);
      if(dbe) {
	dsk->db_entry = (struct harddrive_entry *)malloc(sizeof(struct harddrive_entry));
	memcpy(dsk->db_entry, dbe, sizeof(struct harddrive_entry));
      }	
    }
  }

  free_database();
  if(tcp_daemon || syslog_interval != 0) {
    do_daemon_mode(ldisks);
  }
  else {
    do_direct_mode(ldisks);
  }

  return 0;
}
