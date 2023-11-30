/* SPDX-FileCopyrightText: 2023 Max Hearnden <max@hearnden.org.uk> */
/* SPDX-License-Identifier: GPL-3.0-or-later */


#define CLONE_NEWUSER 0x10000000
#define CLONE_NEWNS 0x00020000
#define MS_BIND 4096
#define MS_REC 16384
#define MNT_DETACH 0x00000002
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef __M2__

#include <bootstrappable.h>

#else

extern int unshare(int flags);

extern int mount(const char *source, const char *target,
  const char *filesystemtype, unsigned long mountflags, const void *data);

#endif

void touch(char *path) {
  int fd = open(path, O_CREAT, 0777);
  if (fd == -1) {
    fputs("Failed to create file ", stderr);
    fputs(path, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
  }
  if (close(fd) != 0) {
    fputs("Failed to close file ", stderr);
    fputs(path, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
  }
}

void mkmount(
  char *source, char *target, char *filesystemtype,
  unsigned mountflags, void *data, int type
) {
  int r = 0;
  if (type) {
    r = mkdir(target, 0755);
  } else {
    touch(target);
  }
  if (r != 0 && r != -17) {
    fputs("Failed to create mountpoint ", stderr);
    fputs(target, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
  }

  r = mount(source, target, filesystemtype, mountflags, data);

  if (r != 0) {
    fputs("Failed to mount directory ", stderr);
    fputs(target, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
  }
}

void set_map(int parent_id, char *path) {
  int fd = open(path, O_WRONLY, 0);
  if (fd == -1) {
    fputs("Failed to open map file ", stderr);
    fputs(path, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
  }

  char *map_contents = calloc(38, sizeof(char));

#ifdef __M2__
  strcpy(map_contents, "0 ");
  char *parent_id_str = int2str(parent_id, 10, 0);
  strcat(map_contents, parent_id_str);
  strcat(map_contents, " 1");
#else
  snprintf(map_contents, 38, "0 %i 1", parent_id);
#endif
  write(fd, map_contents, strlen(map_contents));
  write(STDOUT_FILENO, map_contents, strlen(map_contents));
  free(map_contents);
  close(fd);
}

void deny_setgroups() {
  int fd = open("/proc/self/setgroups", O_WRONLY, 0777);
  if(fd == -1) {
    fputs("Failed to open /proc/self/setgroups\n", stderr);
    exit(EXIT_FAILURE);
  }
  write(fd, "deny", 4);
  close(fd);
}

int main(int argc, char **argv) {
  if(argc <= 1) {
    fputs("Expected at least one argument: command\n", stderr);
    exit(EXIT_FAILURE);
  }
  char *cwd = get_current_dir_name();
  /* Do nothing if cwd is already root */
  if (strcmp(cwd, "/")) {
    int uid = geteuid();
    int gid = getegid();
    /* Don't create a user and mount namespace if we are already root */
    if (uid != 0) {
      if (unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
        fputs("Failed to create user and mount namespaces\n", stderr);
        exit(EXIT_FAILURE);
      }
      /* Prevent the use of setgroups and make gid_map writeable */
      deny_setgroups();
      /* Map the root user in the user namespace to our user id */
      set_map(uid, "/proc/self/uid_map");
      /* Map the root group in the user namespace to our group id */
      set_map(gid, "/proc/self/gid_map");
    }
    int r = mkdir("dev", 0755);
    if (r != 0 && r != -17) {
      fputs("Failed to create dev folder\n", stderr);
      exit(EXIT_FAILURE);
    }
#if !__uefi__
    mkmount ("/dev/null", "dev/null", "", MS_BIND, NULL, 0);
    mkmount ("/dev/zero", "dev/zero", "", MS_BIND, NULL, 0);
    mkmount ("/dev/random", "dev/random", "", MS_BIND, NULL, 0);
    mkmount ("/dev/urandom", "dev/urandom", "", MS_BIND, NULL, 0);
    mkmount ("/dev/ptmx", "dev/ptmx", "", MS_BIND, NULL, 0);
    mkmount ("/dev/tty", "dev/tty", "", MS_BIND, NULL, 0);
    mkmount ("tmpfs", "dev/shm", "tmpfs", 0, NULL, 1);
    mkmount ("/proc", "proc", "", MS_BIND | MS_REC, NULL, 1);
    mkmount ("/sys", "sys", "", MS_BIND | MS_REC, NULL, 1);
    mkmount ("tmpfs", "tmp", "tmpfs", 0, NULL, 1);
#endif
    if (chroot (".") != 0) {
      fputs("Failed to chroot into .\n", stderr);
      exit(EXIT_FAILURE);
    }
  }
  free(cwd);


  char **newenv = malloc(3 * sizeof(char *));
  int newenv_index = 0;
  if (newenv == NULL) {
    fputs("Failed to allocate space for new environment\n", stderr);
    exit(EXIT_FAILURE);
  }

  char *ARCH = getenv("ARCH");
  if (ARCH != NULL) {
    newenv[0] = malloc(6 + strlen(ARCH));
    if (newenv[0] == NULL) {
      fputs("Failed to allocate space for new environment\n", stderr);
      exit(EXIT_FAILURE);
    }
    strcpy(newenv[0], "ARCH=");
    strcpy(newenv[0] + 5, ARCH);
    newenv_index += 1;
  }

  char *ARCH_DIR = getenv("ARCH_DIR");
  if (ARCH_DIR != NULL) {
    newenv[newenv_index] = malloc(10 + strlen(ARCH_DIR));
    if (newenv[newenv_index] == NULL) {
      fputs("Failed to allocate space for new environment\n", stderr);
      exit(EXIT_FAILURE);
    }
    strcpy(newenv[newenv_index], "ARCH_DIR=");
    strcpy(newenv[newenv_index] + 9, ARCH_DIR);
    newenv_index += 1;
  }

  newenv[newenv_index] = NULL;


#ifdef __M2__
#if __uefi__
  return spawn (argv[1], argv + sizeof(char *), newenv);
#else
  return execve (argv[1], argv + sizeof(char *), newenv);
#endif
#else
  return execve (argv[1], argv + 1, newenv);
#endif
}