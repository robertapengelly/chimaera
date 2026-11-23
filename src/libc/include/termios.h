/******************************************************************************
 * @file            termios.h
 *****************************************************************************/
#ifndef     _TERMIOS_H
#define     _TERMIOS_H

#define     NCCS                        19

#define     VEOF                        4
#define     VTIME                       5
#define     VMIN                        6

#define     ISIG                        0000001
#define     ICANON                      0000002
#define     ECHO                        0000010
#define     IEXTEN                      0100000

#define     IXON                        0002000
#define     ICRNL                       0000400

#define     TCSANOW                     0
#define     TCSADRAIN                   1
#define     TCSAFLUSH                   2

typedef     unsigned char               cc_t;

typedef     unsigned int                tcflag_t;
typedef     unsigned int                speed_t;

struct termios {

    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    
    cc_t c_line;
    cc_t c_cc[NCCS];

};

int tcsetattr (int fd, int optional_actions, const struct termios *termios_p);
int tcgetattr (int fd, struct termios *termios_p);

#endif      /* _TERMIOS_H */
