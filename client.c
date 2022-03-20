#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
long elapse(struct timespec start, struct timespec end)
{
    return ((long) 1.0e+9 * end.tv_sec + end.tv_nsec) -
           ((long) 1.0e+9 * start.tv_sec + start.tv_nsec);
}
int main()
{
    long long sz;

    char buf[500];
    // char write_buf[] = "testing writing";
    int offset = 500; /* TODO: try test something bigger than the limit */

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }
    /*
    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }
    */

    struct timespec t1, t2;
    // int count = 0;
    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_REALTIME, &t1);
        sz = read(fd, buf, 500);
        clock_gettime(CLOCK_REALTIME, &t2);
        printf("%d ", i);
        // this is transporting time between kernel and user space
        printf("%lld ", elapse(t1, t2) - sz);
        // this is executing time in user space
        printf("%ld ", elapse(t1, t2));
        // this is executing time in kernel
        printf("%lld \n", sz);
        /*
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%s.\n",
               i, buf);
               */
    }


    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, buf, 500);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%s.\n",
               i, buf);
    }


    close(fd);
    return 0;
}
