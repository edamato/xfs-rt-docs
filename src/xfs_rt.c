#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <xfs/xfs.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#define XFS_SUPER_MAGIC       0x58465342

static const char* FMT_ERROR_CL= "\033[0;31mERROR: %s\033[0m\n";
static const char* FMT_ERROR_BW= "ERROR: %s\n";
static const char* FMT_WARN_CL= "\033[0;31mWARN: %s\033[0m\n";
static const char* FMT_WARN_BW= "WARN: %s\n";
static const char* FMT_INFO_CL= "\033[0;32mINFO: %s\033[0m\n";
static const char* FMT_INFO_BW= "INFO: %s\n";

enum level {
    INFO,
    WARN,
    ERROR
};

/* C99 */
bool stderr_is_term(void) {
    struct stat s;
    if ( fstat(2, &s) < 0) {
        return false;
    }

    if( S_ISCHR(s.st_mode)) {
        return true;
    }

    return false;
}

void log_fmt(enum level l, const char *fmt_usr, va_list ap) {

    const char * fmt_mask;
    char * fmt_str;

    switch (l) {
    case INFO :
        fmt_mask = stderr_is_term() ? FMT_INFO_CL : FMT_INFO_BW;
        break;
    case WARN :
        fmt_mask = stderr_is_term() ? FMT_WARN_CL : FMT_WARN_BW;
        break;
    case ERROR :
        fmt_mask = stderr_is_term() ? FMT_ERROR_CL : FMT_ERROR_BW;
        break;
    default :
        /* invalid levels default to error */
        fmt_mask = stderr_is_term() ? FMT_ERROR_CL : FMT_ERROR_BW;
        dprintf(2,fmt_mask, "unknown log level, defaulting to ERROR");
        break;
    }

    fmt_str = malloc(sizeof(char)*(strlen(fmt_usr)+strlen(fmt_mask)-2));
    if (! fmt_str) {
        dprintf(2,fmt_mask, "failed to malloc log formatter");
        exit(1);
    }

    if (snprintf(fmt_str,strlen(fmt_mask)+strlen(fmt_usr), fmt_mask, fmt_usr) < 0) {
        dprintf(2,fmt_mask, "failed to apply mask formatter");
        exit(1);
    }

    vdprintf(2, fmt_str, ap);
    free(fmt_str);
}


void info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_fmt(INFO, fmt, ap);
    va_end(ap);
}


void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_fmt(WARN, fmt, ap);
    va_end(ap);
}

void error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_fmt(ERROR, fmt, ap);
    va_end(ap);
}

void exit_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_fmt(ERROR, fmt, ap);
    va_end(ap);

    exit(1);
}


bool target_is_xfs(const char* target) {

    /*
       should prob simplify everyone's lives and just use (from man 3 xfsctl):
       int platform_test_xfs_fd(int fd);
       int platform_test_xfs_path(const char *path);
    */

    struct stat s;
    struct statfs st;
    char* basedir;

    basedir = malloc(strlen(target)*sizeof(char));
    if (!basedir) {
        exit_error("failed to allocate basedir");
    }

    strncpy(basedir, target, strlen(target)*sizeof(char));
    basedir = dirname(basedir);

    if (stat(basedir, &s)<0) {
        exit_error("failed to stat basedir");
    }

    if (!S_ISDIR(s.st_mode)) {
        exit_error("basedir not a dir");
    }

    if (statfs(basedir, &st) < 0 ) {
        exit_error("failed to statfs basedir");
    }

    if (st.f_type == XFS_SUPER_MAGIC) {
        return true;
    }

    return false;
}

int main(int argc, char * argv[])
{

    int fd;
    struct stat s;
    struct fsxattr xa;
    bool dirtarget=false;
    if (argc <= 1) exit_error("no file path given");

    char* target = argv[1];

    if (! target_is_xfs(target)) {
        exit_error("target %s is not on XFS", target);
    }

    if ( stat(target, &s) < 0 ) {
        warn("file does not exist, and will be created.");
        fd = open(target, O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    } else {
        if (S_ISREG(s.st_mode)) {
            info("target %s is a regular file", target);
            if(s.st_size>0) {
                exit_error("file size must be zero prior to setting RT flag");
            }
            fd = open(target, O_RDWR);
        } else if (S_ISDIR(s.st_mode)) {
            dirtarget = true;
            info("target is a directory. Inheritance flag will be applied");
            fd = open(target, O_DIRECTORY);
        } else {
            exit_error("target file type not supported: %d", s.st_mode);
        }
    }

    if (fd < 0) {
        exit_error("failed to open target");
    }

    if (xfsctl(target, fd, XFS_IOC_FSGETXATTR, &xa) < 0 ) {
        close(fd);
        exit_error("failed to read xfs xattr from target");
    }

    info("%s - flags: %x", target, xa.fsx_xflags);
    if (xa.fsx_xflags && XFS_XFLAG_REALTIME) {
        info("RT flag is set for %s", target);
        close(fd);
        exit(0);
    }

    info("Setting RT flag on target: %s", target);
    if (dirtarget) {
        xa.fsx_xflags |= XFS_XFLAG_RTINHERIT;
    } else {
        xa.fsx_xflags |= XFS_XFLAG_REALTIME;
    }

    if (xfsctl(target, fd, XFS_IOC_FSSETXATTR, &xa) < 0 ) {
        close(fd);
        exit_error("failed to set xfs xattr on target");
    }

    exit(0);
}
