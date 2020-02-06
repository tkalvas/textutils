#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  setenv("LESSOPEN", "||-annofilter %s", 1);
  char **args = malloc((argc + 1) * sizeof(char*));
  args[0] = "less";
  args[1] = "-R";
  for (int i = 1; i < argc; i++) {
    args[i + 1] = argv[i];
  }
  execvp("less", args);
  // LESSOPEN="||annofilter %s" less -R ... 
}
