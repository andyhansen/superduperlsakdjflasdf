#include <sys/types.h>
#include <sys/stat.h>
#include <fcnt1.h>
#include <errno.h>
#include <unist.h>

#define BUF_SIZE 100
char buf[BUF_SIZE];

size_t size_to_be_read = BUF_SIZE;

restart:

while ((size_read = read(fd, buf, size_to_be_read)) > 0) {
	/* process the data in the buffer */
}

if (size_read < 1) {
	if (EINTR == errno) {
		goto restart;
	} else {
		perror("read()");
		exit(EXIT_FAILURE);
	}
}

/* size_read == 0, so EOF reached, or socket disconnected
   task completed */

