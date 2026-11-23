/******************************************************************************
 * @file            termios.c
 *****************************************************************************/
#include    <sys/ioctl.h>

#include    <errno.h>
#include    <termios.h>

int tcgetattr (int fd, struct termios *termios_p) {
    return ioctl (fd, TCGETS, termios_p);
}

int tcsetattr (int fd, int optional_actions, const struct termios *termios_p) {

    if (optional_actions != TCSANOW) {
    
        errno = EINVAL;
        return -1;
    
    }
    
    return ioctl (fd, TCSETS, termios_p);

}
