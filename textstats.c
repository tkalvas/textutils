#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *help_text =
  "textstats [-hr] [--] <file>*\n"
  "Checks encoding and line endings, counts lines, etc.\n"
  "  -h            Print this help text\n"
  "  -r            Use color codes in output\n";

int use_color = 0;

char *foreground_red = "\033[31m";
char *foreground_green = "\033[32m";
char *foreground_yellow = "\033[33m";
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

void warn_vprintf(char *fmt, va_list ap) {
  color_vprintf(foreground_yellow, fmt, ap);
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

void warn_printf(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  warn_vprintf(fmt, ap);
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

void parse_options(int argc, char **argv) {
  while (1) {
    static struct option long_options[] =
      {
       {"help", no_argument, 0, 'h'},
       {"color", no_argument, 0, 'r'},
       {}
      };
    int option_index = 0;
    int c = getopt_long(argc, argv, "hr", long_options, &option_index);
    if (c == -1) break;
    switch (c) {
    case 'h':
      fprintf(stderr, "%s", help_text);
      exit(0);
    case 'r':
      use_color = 1;
      break;
    }
  }
}

char buffer[65536];
int buffer_pos = 0;
int buffer_len = 65536;

int ulen = 1;
int umin;
int umax;
int u;

int utf8_missing_continuation_count = 0;
int utf8_orphan_continuation_count = 0;
int utf8_overlong_count = 0;
int utf8_upper_control_count = 0;
int utf8_illegal_count = 0;

int last_byte_cr = 0;
int last_byte_whitespace = 0;

int line_count = 0;
int windows_line_count = 0;
int trailing_whitespace_count = 0;
int null_char_count = 0;
int control_count = 0;
int upper_control_count = 0;
int upper_printable_count = 0;
int latin1_finnish_count = 0;

void consume(int end) {
  for (int i = 0; i < buffer_pos; i++) {
    int ch = (int)buffer[i] & 255;

    if (ulen > 1 && ((ch & 0xc0) != 0x80)) {
      utf8_missing_continuation_count++;
    }
    if (!(ch & 0x80)) { // leading 1
      ulen = 1;
    } else if (!(ch & 0x40)) { // continuation
      if (ulen < 2) {
        utf8_orphan_continuation_count++;
      } else {
        u <<= 6;
        u |= ch & 0x3f;
        ulen--;
        if (ulen == 1) {
          if (u < umin)
            utf8_overlong_count++;
          if (u >= 0x80 && u < 0xa0)
            utf8_upper_control_count++;
        }
      }
    } else if (!(ch & 0x20)) { // leading 2
      u = ch & 0x1f;
      ulen = 2;
      umin = 0x80;
      umax = 0x7ff;
    } else if (!(ch & 0x10)) { // leading 3
      ulen = 3;
      umin = 0x800;
      umax = 0xffff;
    } else if (ch < 0xf5) { // leading 4
      ulen = 4;
      umin = 0x10000;
      umax = 0x10ffff;
    } else { // illegal 0xf5-0xff
      utf8_illegal_count++;
      ulen = 1;
    }

    if (ch == '\n') {
      if (last_byte_cr)
        windows_line_count++;
      if (last_byte_whitespace)
        trailing_whitespace_count++;
      line_count++;
    }

    last_byte_cr = ch == '\r';
    if (ch != '\r')
      last_byte_whitespace = ch == '\t' || ch == ' ';

    if (!ch) {
      null_char_count++;
    }

    if (ch > 0 && ch < ' ' && ch != '\r' && ch != '\n' && ch != '\t')
      control_count++;

    if (ch >= 0x80 && ch < 0xa0)
      upper_control_count++;

    if (ch >= 0xa0 && ch < 0x100)
      upper_printable_count++;

    if (ch == 0xc4 || ch == 0xc5 || ch == 0xd6 ||
        ch == 0xe4 || ch == 0xe5 || ch == 0xf6)
      latin1_finnish_count++;
  }
  buffer_pos = 0;
}

void run_fd(int fd) {
  while (1) {
    int len = read(fd, buffer + buffer_pos, buffer_len - buffer_pos);
    if (len == -1)
      errno_printf("cannot read");
    if (!len) break;
    buffer_pos += len;
    consume(0);
  }
  if (buffer_pos)
    consume(1);
}

void run(int index, int argc, char **argv) {
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
  info_printf("%d lines\n", line_count);
  if (windows_line_count)
    warn_printf("%d windows line endings\n", windows_line_count);
  if (null_char_count)
    err_printf("%d null characters\n", null_char_count);
  if (control_count)
    err_printf("%d control characters\n", control_count);
  if (upper_control_count)
    warn_printf("%d upper control characters\n", upper_control_count);
  if (trailing_whitespace_count)
    warn_printf("%d trailing whitespaces\n", trailing_whitespace_count);

  if (utf8_missing_continuation_count)
    err_printf("%d missing utf8 continuation bytes\n",
               utf8_missing_continuation_count);
  if (utf8_orphan_continuation_count)
    err_printf("%d orphan utf8 continuation bytes\n",
               utf8_orphan_continuation_count);
  if (utf8_overlong_count)
    err_printf("%d overlong utf8 encodings\n", utf8_overlong_count);
  if (utf8_upper_control_count)
    err_printf("%d utf8 upper control characters\n", utf8_upper_control_count);
  if (utf8_illegal_count)
    err_printf("%d illegal utf8 encodings\n", utf8_illegal_count);
  if (upper_printable_count) {
    if (100 * latin1_finnish_count / upper_printable_count > 80)
      info_printf("%d/%d finnish letters out of upper printables\n",
                  latin1_finnish_count, upper_printable_count);
    else
      warn_printf("%d/%d finnish letters out of upper printables\n",
                  latin1_finnish_count, upper_printable_count);
  }
  return 0;
}
