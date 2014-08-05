#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define BUF_SIZE 100
char buf[BUF_SIZE];
size_t size_to_be_read = BUF_SIZE;

int main (int argc, const char* argv[]) {
  int file = STDIN_FILENO;
  if (argc > 1) file = open(argv[1], O_RDONLY);
  if (file == -1) {
    perror("open()");
    exit(EXIT_FAILURE);
  }

  int size_read;
restart:
  while ((size_read = read(file, buf, size_to_be_read)) > 0) {
    /* START writing to stdout part */
    int size_remaining = size_read;
    int size_written = 0;
    int size_written_this_time;
    while ((size_written_this_time = write(STDOUT_FILENO, &buf[size_written], size_remaining))
              < size_remaining) {
      if (size_written_this_time < 0) {
        /* something went wrong */
        if (EINTR == errno) {
          continue; /* interrupted, start again */
        } else {
          perror("write()");
          exit(EXIT_FAILURE);
        }
      } else {
        size_written += size_written_this_time;
        size_remaining -= size_written_this_time;
      }
    }
    /* END of writing to stdout part */
  }
  if (size_read == 0) {
    /* size_read == 0, so EOF reached, or socket disconnected */
    close(file); /* should add error handling around closing the file */
    exit(EXIT_SUCCESS);
  } else if (EINTR == errno) {
    goto restart; /* read() was interrupted by a signal, so let's try again */
  } else {
    perror("read()"); /* other error, so print the message and then quit */
    exit(EXIT_FAILURE);
  }
}
