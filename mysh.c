/*
 * mysh.c
 */

// Library inclusions
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <stdlib.h>
#include <sys/wait.h>
#define CYN  "\x1B[36m"
#define RESET "\x1B[0m"


/* Struct definition
   Holds information about a command & array of 11 arguments (including the
   program name as the first argument) */
typedef struct {
  char* command_name;
  char* command_arg_string[11];
  char* input_file;
  char* output_file;
  int is_trunc;
} command_struct;

/* Function headers */
static void output_prompt();
static char **split_arguments_by_pipe(char *input_string, int);
int is_output_redirect(char *);
int is_output_redirect_append(char *);
int is_input_redirect(char *);
int is_pipe(char *);
int count_char(char *, char);
static void run_input(command_struct**, int);
static void format_command_info(char *, command_struct *, int);
static void execute_non_piped(command_struct**, int, int);

/*
 * Drive logic for shell
 */
int main(int argc, char *argv[])
{
  char s[4096];
  output_prompt();
  pid_t pid; // heh
  int wait_int;

  // Break input into commands and create array of command structs
  while(fgets(s, 4096, stdin) != 0) {

    strtok(s, "\n");
    if (strcmp(s, "exit") == 0) {
      exit(0);
    }
    if (strcmp(s, "\n") == 0) {
      output_prompt();
      continue;
    }

    int pipe_count = count_char(s, '|');
    char** arg_array = split_arguments_by_pipe(s, pipe_count);
    command_struct **all_cmds = malloc(sizeof(command_struct) * (pipe_count + 1));

    // For each command string
    for(int i = 0; i <= pipe_count; i++) {

      // Declare a command struct with malloc
      command_struct *cmd_struct = malloc(sizeof(command_struct));

      // Pointer to argument array
      char *cmd = arg_array[i];

      // Count number of parts of command (should be spaces + 1)
      int command_pieces = count_char(cmd, ' ') + 1;
      format_command_info(cmd, cmd_struct, command_pieces);
      all_cmds[i] = cmd_struct;
    }

    if ((pid = fork()) < 0) {
      printf("Forking failure. Aborting\n");
    }
    if (!(pid)) {
      run_input(all_cmds, pipe_count);
    }
    else{
      wait(&wait_int);
    }

    // Free heap memory
    for (int i = 0; i <= pipe_count; i++) {
      free(all_cmds[i]);
    }
    free(arg_array);
    output_prompt();

  }
  printf("\n");
}

/* Args: Command points to the string of command contents, cmd_strct points to an
   empty struct that has memory already set aside for it. Pieces is an integer
   that represents the number of different parts of the command.

   This function iterates through each piece of the command and fills in the
   struct accordingly.*/
static void format_command_info(char *command, command_struct *cmd_strct, int pieces) {

  char *p; // Will be used to point to tokens in command delimited by " ".

  // For each token in the command, record necessary info in struct. Operators
  // '<', '>', '>>' are used to determine placement of i/o files
  strtok(command, "\n");
  for(int i = 0; i < pieces; i++) {
    if (i) {                    // Get next token
      p = strtok(NULL, " ");
    }
    if (!(i)) {                 // If first iteration, initialize using str name
      p = strtok(command, " ");
      cmd_strct->command_name = p;
    }
    else if (is_output_redirect_append(p)) {
      cmd_strct->is_trunc = 0;
      p = strtok(NULL, " ");        // Record next token as output file
      cmd_strct->output_file = p;
      i++;                          // Increment i because used next token
    }
    else if (is_output_redirect(p)){
      cmd_strct->is_trunc = 1;
      p = strtok(NULL, " ");        // Record next token as output file
      cmd_strct->output_file = p;
      i++;                          // Increment i because used nex token
    }
    else if (is_input_redirect(p)) {
      p = strtok(NULL, " ");        // Record next token as output file
      cmd_strct->input_file = p;
      i++;                          // Increment i because used nex token
    }
    else {                          // If not an operator, then piece is an
      for(int j = 1; j < 11; j++) { // arg and should be added to arg array
        if (!(cmd_strct->command_arg_string[j])) {
          cmd_strct->command_arg_string[j] = p;
          break;
        }
      }
    }
  }
  // Default first argument is program name
  cmd_strct->command_arg_string[0] = cmd_strct->command_name;
}

/* Function that arranges commands, file descriptors, and processes */
static void run_input(command_struct** commands, int pipe_num) {
  int in, pipe_fd[2];
  pid_t pid;
  int i;
  in = 0;

  // Fork each of the commands except the last
  for (i = 0; i < (pipe_num); i++) {
    int exit_val;

    // Open up a pipe that forks will use to write input to the next ones.
    // Each child fork will write to it and then close the writing end, and in
    // will be set to the reading end for the next child.
    if ((pipe(pipe_fd)) < 0) {
      perror("pipe");
      exit(1);
    }
    // Fork off a child
    if((pid = fork()) < 0) {
      perror("fork");
      exit(2);
    }
    // For the child:
    if (!(pid)) {

      // If 'in' is stdin (0), this is the first command so check if it was given
      // an input redirect and if so, use dup on a fd for the input file
      if(!(in)) {
        if (commands[i]->input_file) {
          if ((in = open(commands[i]->input_file, O_RDONLY)) < 0) {
            perror("open");
            exit(2);
          }
          }
      }
      dup2(in, 0);
      close(in);
      // If writing direction of the pipe is not set to standard out, dup it
      // such that output will be piped to the right location
      if (pipe_fd[1] != 1) {
        dup2(pipe_fd[1], 1);
        close(pipe_fd[1]);
      }
      // Execute this command
      int res;
      if ((res = execvp(commands[i]->command_name,
      commands[i]->command_arg_string)) < 0) {
        perror("execvp");
        exit(3);
      }
    }
    // For the parent
    else {
      wait(&exit_val);
      close(pipe_fd[1]); // Close writing
      in = pipe_fd[0];   // Set "in" to reading end so next child will
                         // read what this iteration's child wrote
    }
  }
  // Execute base case (command not piped into other command)
  execute_non_piped(commands, pipe_num, in);
}

/* Execute a command that is not piping output into another command, Takes
   the array of command structs, number of pipes, and in fd (from run_input)
   and adjusts its input/output file descriptors based on piping in and
   input/output redirection.*/
static void execute_non_piped(command_struct **commands, int pipe_num, int in){
  int idx = pipe_num;
  // If input is being piped in, adjust dup the output
  if (in != 0) {
    dup2(in, 0);
  }
  // If there is only one command, it may use input redirection
  if (commands[idx]->input_file) {
    int in_fd = open(commands[idx]->input_file, O_RDONLY);
    dup2(in_fd, 0);
    close(in_fd);
    }
  // Output redirection
  if (commands[idx]->output_file) {
      int out_fd;
      if (commands[idx]->is_trunc) {
        if((out_fd = open(commands[idx]->output_file, O_WRONLY | O_CREAT |
        O_TRUNC,0666)) < 0) {
          perror("open");
          exit(6);
        }
      }
      else {
        if((out_fd = open(commands[idx]->output_file, O_WRONLY | O_CREAT, 0666)) < 0) {
          perror("open");
          exit(6);
        }
        if ((lseek(out_fd, 0, SEEK_END)) < 0) {
          perror("lseek");
          exit(7);
        }
      }

      // Change output file descriptor from 1 if output redirection occurs
      dup2(out_fd, 1);
      close(out_fd);
    }

  // Execute the command
  if ((execvp(commands[idx]->command_name,
  commands[idx]->command_arg_string)) < 0) {
    perror("execvp");
    exit(8);
  }
}

/* Function for breaking up arguments */
static char ** split_arguments_by_pipe(char *input_string, int num_pipes){
  char** arg_array = malloc(sizeof(char *) * (num_pipes + 1)); // Create an array of strings
  char *p = strtok(input_string, "|");
  if (num_pipes) {
    p[strlen(p)-1] = '\0';
  }
  arg_array[0] = p;
  for (int i = 1; i <= num_pipes; i++) {
    p = strtok(NULL, "|");
    if (i != num_pipes) {
      p[strlen(p)-1] = '\0';
    }
    arg_array[i] = (p + 1);
  }
  return arg_array;
  }


/* Output prompt as current absolute path */
static void output_prompt() {
  char buf[200];
  getcwd(buf, 200);
  printf(CYN "[%s]$ " RESET, buf);
}

/*******************************************************
Utility functions for checking operation types and counts
********************************************************/

/* Return 1 if string is output redirection else 0 */
int is_output_redirect(char* arg) {
  if ((strlen(arg) == 1) && (*arg == '>')) {
    return 1;
  }
  return 0;
}

/* Return 1 if string is output redirection non-truncated else 0 */
int is_output_redirect_append(char* arg) {
  if ((strlen(arg) == 2) && (!(strcmp(arg, ">>")))) {
    return 1;
  }
  return 0;
}

/* Return 1 if string is input redirection else 0 */
int is_input_redirect(char* arg) {
  if ((strlen(arg) == 1) && (*arg == '<')) {
    return 1;
  }
  return 0;
}
/* Return 1 if string is pipe else 0 */
int is_pipe(char* arg) {
  if ((strlen(arg) == 1) && (*arg == '|')) {
    return 1;
  }
  return 0;
}

/* Return number of occurences of a given char in a string */
int count_char(char* count_string, char count_char) {
  int counter = 0;
  for (int i = 0; (*(count_string + i)); i++) {
    if (*(count_string + i) == count_char) {
      counter++;
    }
  }
  return counter;
}
