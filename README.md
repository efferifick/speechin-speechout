# Speech-in Speech-Out

Proof of concept repo that allows one to replace the standard fgets and fprintf functions with implementations that allow the user to input via speech recognition and the program to output via voice synthesis.

## Dependencies

* espeak
* deepspeech
* portaudio
* curses

## Example:

```c
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
```

```bash
gcc main3.c -lportaudio -lcurses -L./ -ldeepspeech -lespeak-ng speech-in-speech-out.o
LD_LIBRARY_PATH=$PWD ./a.out
```

## Possible extensions

Not sure if it makes sense to make a GCC plugin to perform this translation automatically since it seems the linker is perfectly capable of doing it itself.

- [ ] Remove magic constants
