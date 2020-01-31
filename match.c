#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int use_color = 0;

char *foreground_red = "\033[31m";
char *foreground_green = "\033[32m";
char *foreground_reset = "\033[39m";
char *attribute_reset = "\033[0m";
char *bold = "\033[1m";

void color_vprintf(char *color, char *fmt, va_list ap) {
  if (use_color) fprintf(stderr, "%s", color);
  vfprintf(stderr, fmt, ap);
  if (use_color) fprintf(stderr, "%s", foreground_reset);
}

void err_vprintf(char *fmt, va_list ap) {
  color_vprintf(foreground_red, fmt, ap);
}

void info_vprintf(char *fmt, va_list ap) {
  color_vprintf(foreground_green, fmt, ap);
}

void err_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  err_vprintf(fmt, ap);
  va_end(ap);
}

void info_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  info_vprintf(fmt, ap);
  va_end(ap);
}

void errno_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  err_vprintf(fmt, ap);
  va_end(ap);
  err_printf(": %s (%d)\n", strerror(errno), errno);
  exit(errno);
}

void exit_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  err_vprintf(fmt, ap);
  va_end(ap);
  exit(1);
}

long str2long(char *str) {
  char *end;
  long res = strtol(str, &end, 10);
  if (!*str || *end)
    exit_printf("cannot convert \"%s\" to long\n", str);
  return res;
}

int file4read(char *name) {
  int fd = open(name, O_RDONLY);
  if (fd == -1)
    errno_printf("cannot open file \"%s\"", name);
  return fd;
}

void *allocate(int size) {
  char *res = malloc(size);
  if (!res)
    exit_printf("cannot allocate %d bytes\n", size);
  return res;
}

long max_columns = 65536L;
int report_count = 0;

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
      report_count = 1;
      break;
    case 'm':
      max_columns = str2long(optarg);
      break;
    case 'r':
      use_color = 1;
      break;
    }
  }
}

char *match_param;
int match_param_len;

char *buffer;
int buffer_pos = 0;
int buffer_len;

int state_binary = 0;

void output_part(char *start, char *ptr) {
  if (report_count || !use_color) return;
  if (ptr > start)
    fwrite(start, 1, ptr - start, stdout);
  printf("%s", bold);
  fwrite(ptr, 1, match_param_len, stdout);
  printf("%s", attribute_reset);
}

void output_tail(char *start, int len) {
  if (report_count || !use_color) return;
  fwrite(start, 1, len, stdout);
}

void output_full(char *line, int len) {
  if (report_count || use_color) return;
  fwrite(line, 1, len, stdout);
}

int consume_line(char *line, int line_len) {
  int match = 0;
  char *prev = line;
  char *start = line;
  int len = line_len;
  while (len >= match_param_len) {
    char *ptr = memchr(start, match_param[0], len - match_param_len + 1);
    if (!ptr) break;
    if (!memcmp(ptr, match_param, match_param_len)) {
      output_part(prev, ptr);
      match += 1;
      prev = start = ptr + match_param_len;
      len = line_len - (start - line);
    } else {
      start = ptr + 1;
      len = line_len - (start - line);
    }
  }
  if (match) output_full(line, line_len);
  if (match) output_tail(prev, line_len - (prev - line));
  return match;
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
  if (state_binary) {
    return consume_binary();
  }
  char *ptr = memchr(buffer, '\n', buffer_pos);
  if (ptr) {
    int match = 0;
    char *line = buffer;
    while (ptr) {
      int len = ptr - line + 1;
      match += consume_line(line, len);
      line += len;
      ptr = memchr(line, '\n', buffer_pos - (line - buffer));
    }
    memmove(buffer, line, buffer_pos - (line - buffer));
    buffer_pos -= (line - buffer);
    return match;
  }
  if (force) {
    state_binary = 1;
    return consume_binary();
  }
  return 0;
}

int match_count = 0;

void run_fd(int fd) {
  while (1) {
    int len = read(fd, buffer + buffer_pos, buffer_len - buffer_pos);
    if (len == -1)
      errno_printf("cannot read");
    if (!len) break;
    buffer_pos += len;
    match_count += consume(buffer_pos == buffer_len);
  }
  if (!state_binary && buffer_pos)
    consume_line(buffer, buffer_pos);
}

void run(int index, int argc, char **argv) {
  if (index == argc)
    exit_printf("no match parameter\n");
  match_param = argv[index++];
  match_param_len = strlen(match_param);
  if (!match_param_len)
    exit_printf("match parameter empty\n");
  if (match_param_len >= max_columns)
    exit_printf("match parameter not less than maximum line length\n");
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
  if (state_binary && match_count) {
    info_printf("binary file matches\n");
  }
  if (!state_binary && report_count) {
    info_printf("%d matches\n", match_count);
  }
  return !match_count;
}
