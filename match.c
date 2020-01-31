#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int color = 0;

char *foreground_red = "\033[31m";
char *foreground_green = "\033[32m";
char *foreground_reset = "\033[39m";
char *attribute_reset = "\033[0m";
char *bold = "\033[1m";

void err_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (color)
    fprintf(stderr, "%s", foreground_red);
  vfprintf(stderr, fmt, ap);
  if (color)
    fprintf(stderr, "%s", foreground_reset);
  va_end(ap);
}

void info_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (color)
    fprintf(stderr, "%s", foreground_green);
  vfprintf(stderr, fmt, ap);
  if (color)
    fprintf(stderr, "%s", foreground_reset);
  va_end(ap);
}

long str2long(char *str) {
  char *end;
  long res = strtol(str, &end, 10);
  if (!*str || *end) {
    err_printf("cannot convert \"%s\" to long\n", str);
    exit(1);
  }
  return res;
}

int file4read(char *name) {
  int fd = open(name, O_RDONLY);
  if (fd == -1) {
    err_printf("cannot open file \"%s\": %s (%d)\n",
               name, strerror(errno), errno);
    exit(errno);
  }
  return fd;
}

void *allocate(int size) {
  char *res = malloc(size);
  if (!res) {
    err_printf("cannot allocate %d bytes\n", size);
    exit(1);
  }
  return res;
}

long max_columns = 65536L;
int count = 0;

void parse_options(int argc, char **argv) {
  while (1) {
    static struct option long_options[] =
      {
       {"count", no_argument, 0, 'c'},
       {"max-columns", required_argument, 0, 'm'},
       {"color", no_argument, 0, 'r'},
       {}
      };
    int option_index = 0;
    int c = getopt_long(argc, argv, "cm:r", long_options, &option_index);
    if (c == -1) break;
    switch (c) {
    case 'c':
      count = 1;
      break;
    case 'm':
      max_columns = str2long(optarg);
      break;
    case 'r':
      color = 1;
      break;
    }
  }
}

char *match_param;
int match_param_len;

char *buffer;
int buffer_pos = 0;
int buffer_len;

int binary = 0;

void output_match(char *line, int len, int pos) {
  if (count) return;
  if (pos > 0)
    fwrite(line, 1, pos, stdout);
  if (color)
    printf("%s", bold);
  fwrite(line + pos, 1, match_param_len, stdout);
  if (color)
    printf("%s", attribute_reset);
  if (pos + match_param_len < len)
    fwrite(line + pos + match_param_len, 1, len - (pos + match_param_len),
           stdout);
}

int consume_line(char *line, int len) {
  if (len < match_param_len) return 0;
  char *ptr = memchr(line, match_param[0], len);
  if (ptr && ptr - line + match_param_len <= len) {
    if (!memcmp(ptr, match_param, match_param_len)) {
      output_match(line, len, ptr - line);
      return 1;
    }
  }
  return 0;
}

int consume_binary() {
  if (buffer_pos < match_param_len) return 0;
  char *ptr = memchr(buffer, match_param[0], buffer_pos);
  if (ptr && ptr - buffer + match_param_len <= buffer_pos) {
    if (!memcmp(ptr, match_param, match_param_len)) {
      buffer_pos = 0;
      return 1;
    }
  }
  memmove(buffer, buffer + buffer_pos - match_param_len + 1,
          match_param_len - 1);
  buffer_pos = match_param_len - 1;
  return 0;
}

int consume(int force) {
  if (binary) {
    return consume_binary();
  }
  char *ptr = memchr(buffer, '\n', buffer_pos);
  if (ptr) {
    int offset = ptr - buffer + 1;
    int match = consume_line(buffer, offset);
    memmove(buffer, ptr, buffer_pos - offset);
    buffer_pos -= offset;
    return match;
  }
  if (force) {
    binary = 1;
    return consume_binary();
  }
  return 0;
}

int global_match = 0;

void run_fd(int fd) {
  while (1) {
    int len = read(fd, buffer + buffer_pos, buffer_len - buffer_pos);
    if (len == -1) {
      err_printf("cannot read: %s (%d)\n", strerror(errno), errno);
      exit(errno);
    }
    if (!len) return;
    buffer_pos += len;
    global_match += consume(buffer_pos == buffer_len);
  }
}

void run(int index, int argc, char **argv) {
  if (index == argc) {
    err_printf("no match parameter\n");
    exit(1);
  }
  match_param = argv[index++];
  match_param_len = strlen(match_param);
  if (!match_param_len) {
    err_printf("match parameter empty\n");
    exit(1);
  }
  if (match_param_len >= max_columns) {
    err_printf("match parameter not less than maximum line length\n");
    exit(1);
  }
  buffer_len = max_columns;
  buffer = allocate(buffer_len);
  if (index == argc) {
    run_fd(0);
  } else {
    while (index < argc) {
      int fd = file4read(argv[index++]);
      run_fd(fd);
      close(fd);
    }
  }
}

int main(int argc, char **argv) {
  parse_options(argc, argv);
  run(optind, argc, argv);
  if (binary && global_match) {
    info_printf("binary file matches\n");
  }
  if (!binary && count) {
    info_printf("%d matches\n", global_match);
  }
  return !global_match;
}
