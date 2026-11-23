/******************************************************************************
 * @file            main.c
 *****************************************************************************/
#include    <stdio.h>
#include    <time.h>

static int days_per_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static char *months[] = {

    "Jan",  "Feb",  "Mar",  "Apr",  "May",  "Jun",
    "Jul",  "Aug",  "Sep",  "Oct",  "Nov",  "Dec"

};

static char *days[] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };

static struct { int year, month, day, hour, min, sec; } tm;
static int qflag;

time_t s_p_min;
time_t s_p_hour;
time_t s_p_day;
time_t s_p_year;

/******************************************************************************
 * @function        cv_time
 *****************************************************************************/
void cv_time (time_t t) {

    tm.year = 0;
    tm.month = 0;
    tm.day = 1;
    tm.hour = 0;
    tm.min = 0;
    tm.sec = 0;
    
    while (t >= s_p_year) {
    
        if (((tm.year + 2) % 4) == 0) {
            t -= s_p_day;
        }
        
        tm.year += 1;
        t -= s_p_year;
    
    }
    
    if (((tm.year + 2) % 4) == 0) {
        days_per_month[1]++;
    }
    
    tm.year += 1970;
    
    while (t >= (days_per_month[tm.month] * s_p_day)) {
        t -= days_per_month[tm.month++] * s_p_day;
    }
    
    while (t >= s_p_day) {
    
        t -= s_p_day;
        tm.day++;
    
    }
    
    while (t >= s_p_hour) {
    
        t -= s_p_hour;
        tm.hour++;
    
    }
    
    while (t >= s_p_min) {
    
        t -= s_p_min;
        tm.min++;
    
    }
    
    tm.sec = (int) t;

}

/******************************************************************************
 * @function        main
 *****************************************************************************/
int main (int argc, char **argv) {

    time_t t;
    
    s_p_min = 60;
    s_p_hour = 60 * s_p_min;
    s_p_day = 24 * s_p_hour;
    s_p_year = 365 * s_p_day;
    
    time (&t);
    cv_time (t);
    
    (void) argc;
    (void) argv;
    
    printf ("%s %s %d %02d:%02d:%02d %d\n", days[(t / s_p_day) % 7], months[tm.month], tm.day, tm.hour, tm.min, tm.sec, tm.year); 
    return 0;

}
