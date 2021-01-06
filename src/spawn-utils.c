/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author "Fulup Ar Foll"
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
*/

#define _GNU_SOURCE

#include "spawn-utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>

#ifndef MEMFD_CREATE_MISSING
    //int memfd_create (const char *__name, unsigned int __flags);
    #include <sys/mman.h>
#else
  // missing from Fedora, OpenSuse, ... !!!
  int memfd_create (const char *name, unsigned int __flags) {
     #include <sys/syscall.h>
     #include <linux/memfd.h>
     long fd;
     if((fd = syscall(SYS_memfd_create, name, MFD_CLOEXEC)) == -1) return 1;
     return 0;
  }
#endif

// Exec a command in a memory buffer and return stdout result as FD
const char* utilsExecCmd (afb_api_t api, const char* source, const char* command) {
	char fdstr[32];
	int fd = memfd_create(source, 0);
	if (fd <0) goto OnErrorExit;

	int pid = fork();
	if (pid != 0) {
		// wait for child to finish
		(void)waitpid(pid, 0, 0);
		lseek (fd, 0, SEEK_SET);
		syncfs(fd);
	} else {
		// redirect stdout to fd and exec command
		char *argv[4];
		argv[0]="shfdexec";
		argv[1]="-c";
		argv[2]=(char*)command;
		argv[3]=NULL;

		dup2(fd, 1);
		close (fd);
		execv("/usr/bin/sh", argv);
		AFB_API_ERROR(api, "hoops: utilsExecCmd exec command return command=%s error=%s\n", command, strerror(errno));
	}

	// argv require string
    snprintf (fdstr, sizeof(fdstr), "%d", fd);
	return strdup(fdstr);

OnErrorExit:
	AFB_API_ERROR(api, "error: utilsExecCmd Fail to exec command=%s\n", command);
	return NULL;
}

int utilsFileStat (const char *filepath, int mode) {
    int err;
    struct stat statbuf;

    err = stat(filepath, &statbuf);
    if (err < 0 || !(statbuf.st_mode & mode)) {
        goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;    
}

ssize_t utilsFileLoad (const char *filepath, char **buffer) {
    int err;
    struct stat statbuf;

    err = stat(filepath, &statbuf);
    if (err < 0 || !(statbuf.st_mode & S_IREAD)) {
        goto OnErrorExit;
    }

    // allocate filesize buffer
    *buffer = 1+ malloc(statbuf.st_size);
    if (! buffer) goto OnErrorExit;

    // open file in readonly
    int fdread = open (filepath, O_RDONLY);
    if (fdread <0) goto OnErrorExit;


    ssize_t count=0, len;
    do {
        len= read(fdread, &buffer[count], statbuf.st_size-count); 

    } while (len < 0 && errno == EINTR);
    close (fdread);
    *buffer[count]='\0'; // close string

    return count;

OnErrorExit:
    return -1;    
}


// if string is not null extract umask and apply
mode_t utilsUmaskSet (const char *mask) {
	mode_t oldmask, newmask;
	if (!mask) {
        oldmask= umask(0);
    } else {
		sscanf (mask, "%o", &newmask);
	}
    // posix umask would need some helpers
	oldmask= umask(newmask);
	return oldmask;
}

// Exec a command in a memory buffer and return stdout result as FD
int utilsExecFdCmd (afb_api_t api, const char* source, const char* command) {
	int fd = memfd_create(source, 0);
	if (fd <0) goto OnErrorExit;

	int pid = fork();
	if (pid != 0) {
		// wait for child to finish
		(void)waitpid(pid, 0, 0);
		lseek (fd, 0, SEEK_SET);
		(void)syncfs(fd);
	} else {
		// redirect stdout to fd and exec command
		char *argv[4];
		argv[0]="UtilsExecFd";
		argv[1]="-c";
		argv[2]=(char*)command;
		argv[3]=NULL;

		dup2(fd, 1);
		close (fd);
		execv("/usr/bin/sh", argv);
		AFB_API_NOTICE(api, "[execv returned] command return command=%s error=%s (utilsExecFdCmd)\n", command, strerror(errno));
        exit(1);
	}
	return (fd);

OnErrorExit:
	AFB_API_NOTICE(api, "[Fail to exec] command=%s (utilsExecFdCmd)\n", command);
	return -1;
}

int utilsTaskPrivileged(void) {

    // test privilege only one
    static int status=-1;
    if (status != -1) return status;

    // try temporally upscale to superuser
    if (seteuid(0) != 0) {
        status = 0;
    } else {
        seteuid(getuid());
        status = 1;
    }
    return status;
}

// write group within corresponding sandbox/subgroup dir FD
int utilsFileAddControl (afb_api_t api, const char *uid, int dirFd, const char *ctrlname, const char *ctrlval) {
    int ctrlfd;
    size_t len, count;

    ctrlfd = openat (dirFd, ctrlname, O_WRONLY);
    if (ctrlfd < 0) {
        if (api)
            AFB_API_NOTICE(api, "[cgroup ctrl not found] sandbox='%s' ctrlname='%s' error=%s (nsCgroupSetControl)", uid, ctrlname, strerror(errno));
        else
            fprintf(stderr, "[cgroup ctrl not found] sandbox='%s' ctrlname='%s' error=%s (nsCgroupSetControl)\n", uid, ctrlname, strerror(errno));
        goto OnErrorExit;
    }

    len= strlen(ctrlval);
    count= write(ctrlfd, ctrlval, len);

    // when error add inod to help debuging
    if (count != len) {
        struct stat statfd;
        fstat (ctrlfd, &statfd);
        if (api)
            AFB_API_NOTICE(api, "[cgroup control refused] sandbox='%s' ctrlname='%s' inode=%ld value=%s error=%s (nsCgroupSetControl)", uid, ctrlname, statfd.st_ino, ctrlval, strerror(errno));
        else
            fprintf(stderr, "[cgroup control sandbox='%s' ctrlname='%s' inode=%ld value=%s error=%s (nsCgroupSetControl)\n", uid, ctrlname, statfd.st_ino, ctrlval, strerror(errno));
        close (ctrlfd);
        goto OnErrorExit;
    }

    close (ctrlfd);
    return 0;

OnErrorExit:
    return 1;
}

