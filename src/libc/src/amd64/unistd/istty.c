/******************************************************************************
 * @file            isatty.c
 *****************************************************************************/
#include    <termios.h>
#include    <unistd.h>

int isatty (int __fd) {

    struct termios termios;
    return tcgetattr (__fd, &termios) == 0;

}
