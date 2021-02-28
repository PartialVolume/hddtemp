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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <netinet/in.h>
#include <syslog.h>

// Application specific includes
#include "hddtemp.h"

#define DELAY                  60.0

int                sks_serv_num = 0;
int *              sks_serv;
int                stop_daemon = 0;

/*******************************************************
 *******************************************************/

void daemon_open_sockets(void)
{
  struct addrinfo*   all_ai;
  struct addrinfo    hints;
  struct addrinfo*   resp;
  char               portbuf[10];
  int                on = 1;
  int                ret;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = af_hint;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  snprintf(portbuf, sizeof(portbuf), "%ld", portnum);
  ret = getaddrinfo(listen_addr, portbuf, &hints, &all_ai);
  if (ret != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
    exit(1);
  }

  /* Count max number of sockets we might open. */
  for (sks_serv_num = 0, resp = all_ai ; resp ; resp = resp->ai_next)
    sks_serv_num++;
  sks_serv = malloc(sizeof(int) * sks_serv_num);
  if (!sks_serv) {
    perror("malloc");
    freeaddrinfo(all_ai);
    exit(1);
  }

  /* We may not be able to create the socket, if for example the
   * machine knows about IPv6 in the C library, but not in the
   * kernel. */
  for (sks_serv_num = 0, resp = all_ai; resp; resp = resp->ai_next) {
    sks_serv[sks_serv_num] = socket(resp->ai_family, resp->ai_socktype, resp->ai_protocol); 
    if (sks_serv[sks_serv_num] == -1)
      /* See if there's another address that will work... */
      continue;

    /* Allow local port reuse in TIME_WAIT */
    setsockopt(sks_serv[sks_serv_num], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    /* Allow binding to a listen address that doesn't exist yet */
    setsockopt(sks_serv[sks_serv_num], SOL_IP, IP_FREEBIND, &on, sizeof(on));

    /* Now we've got a socket - we need to bind it. */
    if (bind(sks_serv[sks_serv_num], resp->ai_addr, resp->ai_addrlen) < 0) {
      /* Nope, try another */
      close(sks_serv[sks_serv_num]);
      continue;
    }

    /* Ready to listen */
    if (listen(sks_serv[sks_serv_num], 5) == -1) {
      perror("listen");
      for (sks_serv_num-- ; sks_serv_num > 0 ; sks_serv_num--)
        close(sks_serv[sks_serv_num]);	      
      freeaddrinfo(all_ai);
      free(sks_serv);
      exit(1);	      
    }
    
    sks_serv_num++;
  }
  
  if (sks_serv_num == 0) {
    perror("socket");
    free(sks_serv);
    freeaddrinfo(all_ai);
    exit(1);
  }
    
  freeaddrinfo(all_ai);
}

void daemon_update(struct disk *ldisks, int nocache) {
  struct disk *      dsk;

  for(dsk = ldisks; dsk; dsk = dsk->next) {
    if(nocache || (difftime(time(NULL), dsk->last_time) > DELAY)) {
      dsk->value = -1;

      if(dsk->type == ERROR)
        dsk->ret = GETTEMP_ERROR;
      else 
        dsk->ret = bus[dsk->type]->get_temperature(dsk);

      time(&dsk->last_time);
    }
  }
}

void daemon_close_sockets(void) {
  int i;
  
  for (i = 0 ; i < sks_serv_num; i++)
    close(sks_serv[i]);
}

void daemon_send_msg(struct disk *ldisks, int cfd) {
  struct disk * dsk;

  daemon_update(ldisks, 0);

  for(dsk = ldisks; dsk; dsk = dsk->next) {
    char msg[128];
    int n;

    switch(dsk->ret) {
    case GETTEMP_NOT_APPLICABLE:
      n = snprintf(msg, sizeof(msg), "%s%c%s%cNA%c*",
                   dsk->drive, separator,
                   dsk->model, separator,
                   separator);
      break;
    case GETTEMP_UNKNOWN:
      n = snprintf(msg, sizeof(msg), "%s%c%s%cUNK%c*",
                   dsk->drive, separator,
                   dsk->model, separator, 
		   separator);
      break;
    case GETTEMP_KNOWN:
      n = snprintf(msg, sizeof(msg), "%s%c%s%c%d%c%c",
                   dsk->drive,          separator,
                   dsk->model,          separator,
                   value_to_unit(dsk),  separator,
                   get_unit(dsk));
      break;
    case GETTEMP_NOSENSOR:
      n = snprintf(msg, sizeof(msg), "%s%c%s%cNOS%c*",
                   dsk->drive, separator,
                   dsk->model, separator,
                   separator);
      break;
    case GETTEMP_DRIVE_SLEEP:
      n = snprintf(msg, sizeof(msg), "%s%c%s%cSLP%c*",
                   dsk->drive, separator,
                   dsk->model, separator,
                   separator);
      break;
    case GETTEMP_ERROR:
    default:
      n = snprintf(msg, sizeof(msg), "%s%c%s%cERR%c*",
                   dsk->drive,                        separator,
                   (dsk->model) ? dsk->model : "???", separator,
                   separator);
      break;
    }
    (void)write(cfd, &separator, 1);
    (void)write(cfd, &msg, n);
    (void)write(cfd, &separator, 1);
  }
}


void daemon_syslog(struct disk *ldisks) {
  struct disk * dsk;

  daemon_update(ldisks, 1);
  
  for(dsk = ldisks; dsk; dsk = dsk->next) {
    switch(dsk->ret) {
    case GETTEMP_KNOWN:
      syslog(LOG_INFO, "%s: %s: %d %c", 
             dsk->drive,
	     dsk->model,
	     value_to_unit(dsk),
	     get_unit(dsk));
      break;
    case GETTEMP_DRIVE_SLEEP:
      syslog(LOG_WARNING, _("%s: %s: drive is sleeping"), 
             dsk->drive,
	     dsk->model);
      break;
    case GETTEMP_NOSENSOR:
    case GETTEMP_UNKNOWN:
      syslog(LOG_WARNING, _("%s: %s: no sensor"), 
             dsk->drive,
	     dsk->model);
      break;
    case GETTEMP_NOT_APPLICABLE:
      syslog(LOG_ERR, "%s: %s: %s", 
             dsk->drive,
	     dsk->model,
             dsk->errormsg);
      break;
    default:
    case GETTEMP_ERROR:
      syslog(LOG_ERR, "%s: %s", 
             dsk->drive,
             dsk->errormsg);
      break;
    }
  }      
}

void daemon_stop(int n) {
  (void)n; /* unused */
  stop_daemon = 1;
}

void do_daemon_mode(struct disk *ldisks) {
  struct disk *      dsk;
  int                cfd;
  int                i, ret, maxfd;
  struct tm *        time_st;
  fd_set             deffds;
  time_t             next_time;

if (!foreground) {
    switch(fork()) {
    case -1:
      perror("fork");
      exit(2);
      break;
    case 0:
      break;
    default:
      exit(0);
    }

    setsid();

    switch(fork()) {
    case -1:
      perror("fork");
      exit(2);
      break;
    case 0:
      break;
    default:
      exit(0);
    }
  }
  if (chdir("/") != 0)
      exit(1);
  umask(0);
  
  /* close standard input and output */
  close(0);
  close(1);
  close(2);
  
  if (tcp_daemon)
    daemon_open_sockets();
  
  if (syslog_interval > 0)
    openlog("hddtemp", LOG_PID, LOG_DAEMON);

  /* redirect signals */
  for(i = 0; i <= _NSIG; i++) {
    switch(i) {
    case SIGSEGV: /* still done */
    case SIGBUS:
    case SIGILL:
      break;
    case SIGPIPE:
      signal(SIGPIPE, SIG_IGN);
      break;
    default:
      signal(i, daemon_stop);
      break;
    }
  }

  /* timers initialization */
  next_time = time(NULL);
  for(dsk = ldisks; dsk; dsk = dsk->next) {
    time(&dsk->last_time);
    time_st = gmtime(&dsk->last_time);
    time_st->tm_year -= 1;
    dsk->last_time = mktime(time_st);
  }
  
  /* initialize file descriptors and compute maxfd */
  FD_ZERO(&deffds);
  maxfd = -1;
  
  for (i = 0 ; i < sks_serv_num; i++) {
    FD_SET(sks_serv[i], &deffds);
    
    if (maxfd < sks_serv[i])
      maxfd = sks_serv[i];
  }

  /* start daemon */
  while(stop_daemon == 0) {
    fd_set fds;
    fds = deffds;

    if (syslog_interval > 0)
    {
      struct timeval tv;
      time_t current_time;

      current_time = time(NULL);
      if (next_time > current_time)
        tv.tv_sec = next_time - current_time;
      else
        tv.tv_sec = 0;
      tv.tv_usec = 0;
      ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
    }
    else 
      ret = select(maxfd + 1, &fds, NULL, NULL, NULL);
    
    if (ret == -1) 
      break;
    else if (ret == 0 && syslog_interval > 0) {
      daemon_syslog(ldisks);
      next_time = time(NULL) + syslog_interval;
    }
    else if (tcp_daemon) {
      struct sockaddr_storage caddr;
      socklen_t sz_caddr;
      sz_caddr = sizeof(struct sockaddr_storage);
    
      for (i = 0 ; i < sks_serv_num; i++) {
        if (FD_ISSET(sks_serv[i], &fds))
          break;
      }
      
      if (i == sks_serv_num)
        continue;
    
      if ((cfd = accept(sks_serv[i], (struct sockaddr *)&caddr, &sz_caddr)) == -1)
        continue;
   
      daemon_send_msg(ldisks, cfd);
      
      close(cfd);
    }
  }
  
  if (tcp_daemon)
    daemon_close_sockets();

  if (syslog_interval > 0)
    closelog();
}

