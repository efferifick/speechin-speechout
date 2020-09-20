#include <stdio.h>
int
main(int argc, char** argv)
{
  fprintf(stdout, "please say your name\n");
  char name[100];
  fgets(name, 100, stdin);
  fprintf(stdout, "hello %s\n", name);
  return 0;
}
