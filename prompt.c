#include <stdio.h>
#include <stdlib.h>

#include <editline/readline.h>
#include <editline/history.h>

int main(int argc, char** argv) {
  puts("Lispy version 0.0.1");
  puts("Press ^C to exit.");

  while(1) {
    char* input = readline("lispy> ");

    add_history(input);

    printf("No you're a %s\n", input);

    free(input);
  }
  
  return 0;
}
