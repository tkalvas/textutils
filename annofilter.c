#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum condition {OK, CONTROL, ENCODING, OVERLONG, HIGH_CONTROL,
                TRAILING_WHITESPACE};

char *markup[] = {
  "\033[0m",
  "\033[41;97m",
  "\033[41;97m",
  "\033[41;97m",
  "\033[41;97m",
  "\033[43m",
};

char *help_text =
  "annofilter [-h]\n"
  "Annotates encoding and other text problems with color codes for less.\n"
  "Reads stdin and writes stdout.\n"
  "  -h            Print this help text\n";

char *foreground_red = "\033[31m";
char *foreground_green = "\033[32m";
char *foreground_yellow = "\033[33m";
char *foreground_reset = "\033[39m";
char *attribute_reset = "\033[0m";
char *bold = "\033[1m";

int use_color = 1;

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
       {}
      };
    int option_index = 0;
    int c = getopt_long(argc, argv, "h", long_options, &option_index);
    if (c == -1) break;
    switch (c) {
    case 'h':
      fprintf(stderr, "%s", help_text);
      exit(0);
    }
  }
}

char buffer[65536];
int buffer_out = 0;
int buffer_pos = 0;
int buffer_len = 65536;

int last_byte_nl = 0;
int last_byte_cr = 0;
int last_byte_whitespace = 0;

enum condition current_condition = OK;

void flush_output(int index) {
  if (index > buffer_out) {
    if (current_condition != OK) printf("%s", markup[OK]);
    current_condition = OK;
    fwrite(buffer + buffer_out, 1, index - buffer_out, stdout);
    buffer_out = index;
  }
}

void bad_preface(enum condition cond, int index) {
  flush_output(index);
  if (cond != current_condition) {
    printf("%s", markup[cond]);
  }
  current_condition = cond;
}

void bad_byte(enum condition cond, int index) {
  bad_preface(cond, index);
  printf("<%02x>", buffer[index] & 255);
  buffer_out = index + 1;
}

void bad_bytes(int count, enum condition cond, int index) {
  for (int i = 0; i < count; i++)
    bad_byte(cond, index + i);
}

void bad_marker(enum condition cond, int index) {
  bad_preface(cond, index);
  printf(" ");
}

void early_out(int index) {
  flush_output(index);
  memmove(buffer, buffer + index, buffer_pos - index);
  buffer_pos -= index;
  buffer_out = 0;
}

void consume() {
  for (int i = 0; i < buffer_pos; i++) {
    int ch = (int)buffer[i] & 255;
    if (!(ch & 0x80)) { // leading 1
      if (ch < ' ' && ch != '\n' && ch != '\t')
        bad_byte(CONTROL, i);
    } else if (!(ch & 0x40)) { // continuation
      bad_byte(ENCODING, i);
    } else if (!(ch & 0x20)) { // leading 2
      if (i + 1 >= buffer_pos) {
        early_out(i);
        return;
      }
      int ch1 = (int)buffer[i+1] & 255;
      if ((ch1 & 0xc0) != 0x80) {
        bad_byte(ENCODING, i);
        continue;
      }
      int u = ((ch & 0x1f) << 6) | (ch1 & 0x3f);
      if (u < 0x80) bad_bytes(2, OVERLONG, i);
      else if (u < 0xa0) bad_bytes(2, HIGH_CONTROL, i);
    } else if (!(ch & 0x10)) { // leading 3
      if (i + 2 >= buffer_pos) {
        early_out(i);
        return;
      }
      int ch1 = (int)buffer[i+1] & 255;
      int ch2 = (int)buffer[i+2] & 255;
      if ((ch1 & 0xc0) != 0x80 || (ch2 & 0xc0) != 0x80) {
        bad_byte(ENCODING, i);
        continue;
      }
      int u = ((ch & 0xf) << 12) | ((ch1 & 0x3f) << 6) | (ch2 & 0x3f);
      if (u < 0x800) bad_bytes(3, OVERLONG, i);
    } else if (ch < 0xf5) { // leading 4
      if (i + 3 >= buffer_pos) {
        early_out(i);
        return;
      }
      int ch1 = (int)buffer[i+1] & 255;
      int ch2 = (int)buffer[i+2] & 255;
      int ch3 = (int)buffer[i+3] & 255;
      if ((ch1 & 0xc0) != 0x80 || (ch2 & 0xc0) != 0x80 || (ch3 & 0xc0) != 0x80) {
        bad_byte(ENCODING, i);
        continue;
      }
      int u = ((ch & 0xf) << 18) | ((ch1 & 0x3f) << 12) | ((ch2 & 0x3f) << 6) |
        (ch3 & 0x3f);
      if (u < 0x10000) bad_bytes(4, OVERLONG, i);
    } else { // illegal 0xf5-0xff
      bad_byte(ENCODING, i);
      continue;
    }

    last_byte_nl = ch == '\n';

    if (ch == '\n' && last_byte_whitespace)
      bad_marker(TRAILING_WHITESPACE, i);

    last_byte_cr = ch == '\r';
    if (ch != '\r')
      last_byte_whitespace = ch == '\t' || ch == ' ';
  }
  flush_output(buffer_pos);
  buffer_out = 0;
  buffer_pos = 0;
}

void run_fd(int fd) {
  while (1) {
    int len = read(fd, buffer + buffer_pos, buffer_len - buffer_pos);
    if (len == -1)
      errno_printf("cannot read");
    if (!len) break;
    buffer_pos += len;
    consume();
  }
  for (int i = 0; i < buffer_pos; i++) {
    bad_byte(ENCODING, i);
  }
}

void run(int index, int argc, char **argv) {
  if (index == argc) {
    run_fd(0);
  } else {
    while (index < argc) {
      char *name = argv[index++];
      if (!strcmp(name, "-")) {
        run_fd(0);
      } else {
        int fd = file4read(name);
        run_fd(fd);
        close(fd);
      }
    }
  }
}

int main(int argc, char **argv) {
  parse_options(argc, argv);
  run(optind, argc, argv);
  return 0;
}
