/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>
#include <assert.h>

#ifndef MEMFD_CREATE_MISSING
    //long memfd_create (const char *__name, unsigned int __flags);
    #include <sys/mman.h>
#else
  // missing from Fedora, OpenSuse, ... !!!
  long memfd_create (const char *name, unsigned int flags) {
     #include <sys/syscall.h>
     #include <linux/memfd.h>
     return (syscall(SYS_memfd_create, name, flags));
  }
#endif

#include <json-c/json.h>

#include "spawn-utils.h"


// Exec a command in a memory buffer and return stdout result as FD
const char* utilsExecCmd (afb_api_t api, const char* target, const char* command, int *filefd) {
	char fdstr[32];

    // create a valid string name for memfd from target name
    strncpy (fdstr, target, sizeof(fdstr));
    for (int idx=0; fdstr[idx] != '\0'; idx++) {
        if (fdstr[idx]=='/') fdstr[idx]=':';
    }
	int fd = (int)memfd_create(target, MFD_ALLOW_SEALING);
	if (fd <= 0) goto OnErrorExit;

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
		fprintf (stderr, "hoops: utilsExecCmd execfd command return command=%s error=%s\n", command, strerror(errno));
	}

	// argv require string
    *filefd=fd;
    char *response;
    if(asprintf (&response, "%d", fd)<0){
        goto OnErrorExit;}
	return response;

OnErrorExit:
	AFB_API_ERROR(api, "error: utilsExecCmd target=%s Fail to exec command='%s' error='%s'\n", target, command, strerror(errno));
	return NULL;
}

int utilsFileModeIs (const char *filepath, int mode) {
    int err;
    struct stat statbuf;

    err = stat(filepath, &statbuf);
    if (err < 0 || !(statbuf.st_mode & mode)) {
        goto OnErrorExit;
    }
    return 1;

OnErrorExit:
    return 0;
}

ssize_t utilsFileLoad (const char *filepath, char **buffer) {
    int err;
    struct stat statbuf;
    char *data;

    err = stat(filepath, &statbuf);
    if (err < 0 || !(statbuf.st_mode & S_IREAD)) {
        goto OnErrorExit;
    }

    // allocate filesize buffer
    data= 1+ malloc(statbuf.st_size);
    if (! data) goto OnErrorExit;

    // open file in readonly
    int fdread = open (filepath, O_RDONLY);
    if (fdread <0) goto OnErrorExit;


    ssize_t count=0, len;
    do {
        len= read(fdread, &data[count], statbuf.st_size-count);
        count += len;

    } while (len < 0 && errno == EINTR);
    close (fdread);
    data[count]='\0'; // close string
    *buffer= data;
    return count;

OnErrorExit:
    fprintf (stderr, "Fail to load file=%s err=%s\n", filepath, strerror(errno));
    *buffer=NULL;
    return 0;
}

// if string is not null extract umask and apply
mode_t utilsUmaskSetGet (const char *mask) {
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
	int fd = (int)memfd_create(source, 0);
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
                if (api)
		        AFB_API_NOTICE(api, "[execv returned] command return command=%s error=%s (utilsExecFdCmd)\n", command, strerror(errno));
                else
		        AFB_NOTICE("[execv returned] command return command=%s error=%s (utilsExecFdCmd)\n", command, strerror(errno));
        exit(1);
	}
	return (fd);

OnErrorExit:
        if (api)
	        AFB_API_NOTICE(api, "[Fail to exec] command=%s (utilsExecFdCmd)\n", command);
        else
	        AFB_NOTICE("[Fail to exec] command=%s (utilsExecFdCmd)\n", command);
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
        if(seteuid(getuid())<0){
            return status;
        }
        status = 1;
    }
    return status;
}

// restore default signal handler behaviour
void utilsResetSigals(void) {
    signal(SIGSEGV, SIG_DFL);
}

// return file inode (use to check if two path are pointing on the same file)
long unsigned int utilsGetPathInod (const char* path) {
    struct stat fstat;
    int status;
    status = stat (path, &fstat);
    if (status <0) goto OnErrorExit;

    return (fstat.st_ino);

OnErrorExit:
    return -1;
}

// write group within corresponding sandbox/subgroup dir FD
int utilsFileAddControl (afb_api_t api, const char *uid, int dirFd, const char *ctrlname, const char *ctrlval) {
    int ctrlfd;
    size_t len, count;

    ctrlfd = openat (dirFd, ctrlname, O_WRONLY);
    if (ctrlfd < 0) {
        if (api)
            AFB_API_NOTICE(api, "[cgroup-ctrl-not-found] sandbox='%s' ctrlname='%s' error=%s (nsCgroupSetControl)", uid, ctrlname, strerror(errno));
        else
            AFB_NOTICE("[cgroup-ctrl-not-found] sandbox='%s' ctrlname='%s' error=%s (nsCgroupSetControl)\n", uid, ctrlname, strerror(errno));
        goto OnErrorExit;
    }

    len= strlen(ctrlval);
    count= write(ctrlfd, ctrlval, len);

    // when error add inod to help debuging
    if (count != len) {
        struct stat statfd;
        fstat (ctrlfd, &statfd);
        if (api)
            AFB_API_NOTICE(api, "[cgroup-control-refused] sandbox='%s' ctrlname='%s' inode=%ld value=%s error=%s (nsCgroupSetControl)", uid, ctrlname, statfd.st_ino, ctrlval, strerror(errno));
        else
            AFB_NOTICE("[cgroup-control-refused] sandbox='%s' ctrlname='%s' inode=%ld value=%s error=%s (nsCgroupSetControl)\n", uid, ctrlname, statfd.st_ino, ctrlval, strerror(errno));
        close (ctrlfd);
        goto OnErrorExit;
    }

    close (ctrlfd);
    return 0;

OnErrorExit:
    return 1;
}


