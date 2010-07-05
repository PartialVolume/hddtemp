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
 * Adapted from a patch sended by : Frederic LOCHON <lochon@roulaise.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <scsi/scsi.h>

// Application specific includes
#include "scsicmds.h"
#include "hddtemp.h"

static int scsi_probe(int device) {
  int bus_num;

  if(ioctl(device, SCSI_IOCTL_GET_BUS_NUMBER, &bus_num))
    return 0;
  else
    return 1;
}

static const char *scsi_model (int device) {
  unsigned char buf[36];

  if (scsi_inquiry(device, buf) != 0)
    return strdup(_("unknown"));
  else {
    return strdup(buf + 8);
  }
}

static enum e_gettemp scsi_get_temperature(struct disk *dsk) {
  int              i;
  int              tempPage = 0;
  unsigned char    buffer[1024];

  /*
    on triche un peu
  */
  dsk->db_entry = (struct harddrive_entry*) malloc(sizeof(struct harddrive_entry));
  if(dsk->db_entry == NULL) {
    perror("malloc");
    exit(-1);
  }
  
  dsk->db_entry->regexp       = "";
  dsk->db_entry->description  = "";
  dsk->db_entry->attribute_id = 0;
  dsk->db_entry->unit         = 'C';
  dsk->db_entry->next         = NULL;

  if (scsi_smartsupport(dsk->fd) == 0) {
    snprintf(dsk->errormsg, MAX_ERRORMSG_SIZE, _("S.M.A.R.T. not available\n"));
    close(dsk->fd);
    dsk->fd = -1;
    return GETTEMP_NOT_APPLICABLE;
  }

  /*
    Enable SMART
  */
  if (scsi_smartDEXCPTdisable(dsk->fd) != 0) {
    snprintf(dsk->errormsg, MAX_ERRORMSG_SIZE, "%s", strerror(errno));
    close(dsk->fd);
    dsk->fd = -1;
    return GETTEMP_ERROR;
  }

  /*
    Temp. capable 
  */  
  if (scsi_logsense(dsk->fd , SUPPORT_LOG_PAGES, buffer, sizeof(buffer)) != 0) {
    snprintf(dsk->errormsg, MAX_ERRORMSG_SIZE, _("log sense failed : %s"), strerror(errno));
    close(dsk->fd);
    dsk->fd = -1;
    return GETTEMP_ERROR;
   }

   for ( i = 4; i < buffer[3] + LOGPAGEHDRSIZE ; i++) {
     if (buffer[i] == TEMPERATURE_PAGE) {
       tempPage = 1;
       break;
     }
   }

   if(tempPage) {
      /* 
	 get temperature (from scsiGetTemp (scsicmd.c))
      */
      if (scsi_logsense(dsk->fd , TEMPERATURE_PAGE, buffer, sizeof(buffer)) != 0) {
	snprintf(dsk->errormsg, MAX_ERRORMSG_SIZE, _("log sense failed : %s"), strerror(errno));
	close(dsk->fd);
	dsk->fd = -1;
	return GETTEMP_ERROR;
      }

      dsk->value = buffer[9];

      return GETTEMP_KNOWN;
   } else {
     return GETTEMP_NOSENSOR;
   }
}

/*******************************
 *******************************/

struct bustype scsi_bus = {
  "SCSI",
  scsi_probe,
  scsi_model,
  scsi_get_temperature
};
