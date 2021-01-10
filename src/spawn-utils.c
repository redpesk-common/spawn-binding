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
#include <assert.h>

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

int utilsFileModeIs (const char *filepath, int mode) {
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


// Extract $KeyName and replace with $Key Env or default Value
static int utilExpandEnvKey (spawnDefaultsT *defaults, int *idxIn, const char *inputS, int *idxOut, char *outputS, int maxlen, void *userdata) {
    char envkey[64];
    char *envval=NULL;
    int index;

    // get envkey from $ to any 1st non alphanum character
    for (int idx=0; inputS[*idxIn] != '\0'; idx++) {

        (*idxIn)++; // intial $ is ignored
        if (inputS[*idxIn] >= '0' && inputS[*idxIn] <= 'z') {
            if (idx == sizeof(envkey)) goto OnErrorExit;
            envkey[idx]= inputS[*idxIn] & ~32; // move to uppercase
        } else {
            (*idxIn)--; // keep input separation character
            envkey[idx]='\0';
            break;
        }
    }

    // Search for a default key
    for (index=0; defaults[index].label; index++) {
        if (!strcmp (envkey, defaults[index].label)) {
            envval = (*(spawnGetDefaultCbT)defaults[index].callback) (defaults[index].label, defaults[index].ctx, userdata);
            if (!envval) goto OnErrorExit;
            for (int jdx=0; envval[jdx]; jdx++) {
                if (*idxOut >= maxlen) goto OnErrorExit;
                outputS[(*idxOut)++]= envval[jdx];
            }
            free (envval);
        }
    }

    // if label was not found but default callback is defined
    if (!envval && defaults[index].callback) {
        envval = (*(spawnGetDefaultCbT)defaults[index].callback) (defaults[index].label, defaults[index].ctx, userdata);
        for (int jdx=0; envval[jdx]; jdx++) {
            if (*idxOut >= maxlen) goto OnErrorExit;
            outputS[(*idxOut)++]= envval[jdx];
        }
        free (envval);
    }

    if (!envval) goto OnErrorExit;
    return 0;

OnErrorExit:
    { // label not expanded, provide some usefull debug information
        int index=0;
        envval="???unset_config_defaults????";
        outputS[index++]='$';
        for (int jdx=0; envkey[jdx]; jdx++) {
            if (index >= maxlen-2) {
            outputS[index] = '\0';
            return 1;
            }
            outputS[index++]= envkey[jdx];
        }
        outputS[index++] = '=';

        for (int jdx=0; envval[jdx]; jdx++) {
            if (index >= maxlen) {
                outputS[index] = '\0';
                return 1;
            }
            outputS[index++]= envval[jdx];
        }
        outputS[index] = '\0';
        return 1;
    }
}

const char *utilsExpandString (spawnDefaultsT *defaults, const char* inputS, const char* prefix, const char* trailer, void *userdata) {
    int count=0, idxIn, idxOut=0;
    char outputS[SPAWN_MAX_ARG_LEN];
    int err;

    if (prefix) {
        for (int idx=0; prefix[idx]; idx++) {
            if (idxOut == SPAWN_MAX_ARG_LEN) goto OnErrorExit;
            outputS[idxOut]=prefix[idx];
            idxOut++;
        }
    }

    // search for a $within string input format
    for (idxIn=0; inputS[idxIn] != '\0'; idxIn++) {

        if (inputS[idxIn] != '$') {
            if (idxOut == SPAWN_MAX_ARG_LEN)  goto OnErrorExit;
            outputS[idxOut++] = inputS[idxIn];

        } else {
            if (count == SPAWN_MAX_ARG_LABEL) goto OnErrorExit;
            err=utilExpandEnvKey (defaults, &idxIn, inputS, &idxOut, outputS, SPAWN_MAX_ARG_LEN, userdata);
            if (err) {
                fprintf (stderr, "ERROR: [utilsExpandString] ==> %s <== (check xxxx-defaults.c)\n", outputS);
                goto OnErrorExit;
            }
            count ++;
        }
    }

    // if we have a trailer add it now
    if (trailer) {
        for (int idx=0; trailer[idx]; idx++) {
            if (idxOut == SPAWN_MAX_ARG_LEN) goto OnErrorExit;
            outputS[idxOut]=trailer[idx];
            idxOut++;
        }
    }

    // close the string
    outputS[idxOut]='\0';

    // string is formated replace original with expanded value
    return strdup(outputS);

  OnErrorExit:
    return NULL;

}

// default basic string expansion
const char *utilsExpandKey (const char* src) {
    if (!src) goto OnErrorExit;
    const char *outputString= utilsExpandString (spawnVarDefaults, src, NULL, NULL, NULL);
    return outputString;

  OnErrorExit:
    return NULL;
}

// default basic string expansion
const char *utilsExpandKeyCtx (const char* src, void *ctx) {
    if (!src) goto OnErrorExit;
    const char *outputString= utilsExpandString (spawnVarDefaults, src, NULL, NULL, ctx);
    return outputString;

  OnErrorExit:
    return NULL;
}


// utilsExpandJson is call within forked process, let's keep a test instance within main process for debug purpose
void utilsExpandJsonDebug (void) {
    const char *response;

    json_object *tokenJ1= json_tokener_parse("{'dirname':'/my/test/sample'}"); assert(tokenJ1);
    json_object *tokenJ2= json_tokener_parse("{ 'filename': '/etc/passwd' }"); assert(tokenJ2);

    response= utilsExpandJson ("%filename%", tokenJ2);    assert(response);

    response= utilsExpandJson ("--%dirname%--", tokenJ1);    assert(response);
    response= utilsExpandJson ("--%dirname%--", tokenJ1);   assert(response);
    response= utilsExpandJson ("--notexpanded=%%dirname%% expanded=%dirname%", tokenJ1);  assert(response);
    response= utilsExpandJson ("--notfound=%filename%%", tokenJ1);  assert(!response);
}

// replace any %key% with its coresponding json value (warning: json is case sensitive)
const char *utilsExpandJson (const char* src, json_object *keysJ) {
    int srcIdx, destIdx=0, labelIdx, expanded=0;
    char dst[SPAWN_MAX_ARG_LEN], label[SPAWN_MAX_ARG_LABEL];
    const char *response;
    json_object *labelJ;

    if (!src || !keysJ) goto OnErrorExit;

    for (srcIdx=0; src[srcIdx]; srcIdx++) {

        // replace "%%" by '%'
        if (src[srcIdx] == '%' && src[srcIdx+1] == '%') {
            dst[destIdx++]= src[srcIdx];
            srcIdx++;
            continue;
        }

        if (src[srcIdx] != '%') {
            dst[destIdx++]= src[srcIdx];

        } else {
            expanded=1;
            labelIdx=0;
            // extract expansion label for source argument
            for (srcIdx=srcIdx+1; src[srcIdx]  ; srcIdx++) {
                if (src[srcIdx] != '%' ) {
                    label[labelIdx++]= src[srcIdx];
                    if (labelIdx == SPAWN_MAX_ARG_LABEL) goto OnErrorExit;
                } else break;
            }

            // close label string and remove trailling '%' from destination
            label[labelIdx]='\0';

            // search for expansion label within keysJ
            labelJ= json_object_object_get (keysJ, label);
            if (!labelJ) goto OnErrorExit;

            // add label value to destination argument
            const char *labelVal= json_object_get_string(labelJ);
            for (labelIdx=0; labelVal[labelIdx]; labelIdx++) {
                dst[destIdx++] = labelVal[labelIdx];
            }
        }
    }
    dst[destIdx++] = '\0';

    // when expanded make a copy of dst into params
    if (!expanded) {
        response=src;
    } else {
        // fprintf (stderr, "utilsExpandJson: '%s' => '%s'\n", src, dst);
        response= strdup(dst);
    }

    return response;

  OnErrorExit:
        return NULL;
}