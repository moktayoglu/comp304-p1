#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define GAME_ARRAY_SIZE 30 // for fibonacci game

const char *sysname = "shellax";

enum return_codes
{
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t
{
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
  {
    printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
  }
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next)
  {
    printf("\tPiped to: %s\n", command->next->name);
    print_command(command->next);
  }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
  if (command->arg_count)
  {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next)
  {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL)
  {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  }
  else
  {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int target_saved = -1;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1)
  {

    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue;                                          // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0)
    {
      struct command_t *c = malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;

    if (arg[0] == '>')
    {
      if (len > 1 && arg[1] == '>')
      {
        redirect_index = 2;
        arg++;
        len--;
      }
      else
        redirect_index = 1;
    }
    if (redirect_index != -1)
    {
      command->redirects[redirect_index] = malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  return 0;
}

void prompt_backspace()
{
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1)
  {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0)
      {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68)
    {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0)
      {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
int process_command(struct command_t *command);
void redirection_part2(struct command_t *command);
int chatroom(struct command_t *command);
int pomodoro(struct command_t *command);
int fib(int n);
void fibonacci_game(int arr[]);
int main()
{
  while (1)
  {
    struct command_t *command = malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}

int process_command(struct command_t *command)
{
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0)
  {
    if (command->arg_count > 0)
    {
      r = chdir(command->args[0]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  if (strcmp(command->name, "chatroom") == 0)
  {
    if (command->arg_count == 2)
    {
      chatroom(command);
    }
    return SUCCESS;
  }

  if (strcmp(command->name, "pomodoro") == 0)
  {
    if (command->arg_count == 3)
    {
      pomodoro(command);
    }
    return SUCCESS;
  }
  
  if(strcmp(command->name, "psvis") == 0 ){
  
  	if(command->arg_count == 2){
  	
  	     pid_t pid_dmesg = fork();
  	     
  	     if(pid_dmesg == 0){
  	     	char *clear_msg_cmd[] = {"sudo", "dmesg", "-c", NULL};
  	     	execvp(clear_msg_cmd[0], clear_msg_cmd);
  	     	
  	     
  	     }else{
  		wait(0);
  	     	pid_t pid_ps = fork();
  	     
  	     	if (pid_ps==0){//Loading the psvis module
  	     	
  	     		char pass_pid[20];
  	     		strcpy(pass_pid, "pid=");
  	     		strcat(pass_pid,command->args[0]);
  	     		char *cmd_invoke[]={"sudo", "insmod", "psvis.ko", pass_pid, NULL};
  	     		//printf("invoke\n");
  	     		execvp(cmd_invoke[0], cmd_invoke);
  	     	
  	     
  	     	}else{
  	     		wait(0);
  	     
  	     		pid_t pid = fork();
  	     	
  	     		if (pid == 0){
  	     		char *com_remove[]={"sudo", "rmmod", "psvis", NULL};
  	     		//remove kernel
  	     	
  	     		execvp(com_remove[0],com_remove);
  	     		
  	     		}else{
  	     			wait(0);
  	     			//redirect output
  	     			int out = open(command->args[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    	        		if (out == -1){
   		 
     		    			printf("Error opening redirect target file!\n");
    				}
    				dup2(out, STDOUT_FILENO); // copy file to stdout
    				close(out);
    				char *dmesg_out[]={"sudo", "dmesg",NULL};
  	     			execvp(dmesg_out[0], dmesg_out);
  	     			
  	     	
  	     		}
  
  	     	}
  	     }
  	   
  	
  	}
  	return SUCCESS;
  	
  }

  if (strcmp(command->name, "fib") == 0)
  {                                 // fibonacci game
    int arr[GAME_ARRAY_SIZE] = {0}; // initialization
    arr[1] = 1;
    fibonacci_game(arr);
    getchar();
    return 0;
  }
  
  int num_pipes = 0;
  int status;
  // PART 2 - piping
  if (command->next)
  {
    struct command_t *tmp = malloc(sizeof(struct command_t));
    memcpy(tmp, command, sizeof(struct command_t));

    while (tmp->next)
    {
      num_pipes++;
      tmp = tmp->next;
    }
  }
  //printf("Number of pipes %d\n", num_pipes);
  //printf("here\n");
  int fd_pipes[2 * num_pipes];
  for (int i = 0; i < num_pipes; i++)
  {
    if (pipe(fd_pipes + i * 2) == -1)
    {
      printf("Error creating the pipe!\n");
      return UNKNOWN;
    }
  }

  for (int i = 0; i < num_pipes + 1; i++)
  {
    pid_t pid = fork();
   
    if (pid == 0) // child
    { 
     
      //print_command(command);
      //printf("%d\n", i);
      // if not last command
      if (i != num_pipes)
      {
        if (command->next)
        {
          dup2(fd_pipes[2 * i + 1], STDOUT_FILENO);
        }
      }
      // if not first command, without a read
      if (i != 0)
      {
        dup2(fd_pipes[2 * i - 2], STDIN_FILENO);
      }

      for (int j = 0; j < 2 * num_pipes; j++)
      {
        close(fd_pipes[j]);
      }

      if (strcmp(command->name, "myuniq") == 0)
      {
       
        char line[20][20];
        int i = 0;
        char ch;
        int j = 0;

        char buf[400];
        read(STDIN_FILENO , buf ,  sizeof(buf));
        int l = 0;
        char* split_request;
       
        split_request = strtok(buf,"\n");
        while(split_request != NULL)
        {
          strcpy(line[l],split_request);
          l++;
          i++; //number of lines
          split_request = strtok(NULL,"\n");

        }
        
        int tot = i;
        int check = 0; //flag var
        int count2 = 0; //for number of occurences
        for (int i = 0; i <= tot; ++i){ 
          if(check!=0){ 
               if((command->arg_count>0) && (((strcmp(command->args[0],"-c")==0)) || (strcmp(command->args[0],"--count")==0))){
                   printf("%d %s\n",count2,line[i-1]);  
               }
               else{
                 printf("%s\n",line[i-1]);
               }

               count2 = 1;
              
          }
          if(check==0){
            count2++;
          }
          check = strcmp(line[i],line[i+1]); //check if consecutive strings are equal
        }
      }


      if (strcmp(command->name, "wiseman") == 0){
    
        printf("wiseman will speak for every %s minutes.\n",command->args[0]);
        FILE *cronjob_hw_file = fopen("cronjob.txt", "w");
        fprintf(cronjob_hw_file, "SHELL=/bin/bash\n");
        fprintf(cronjob_hw_file, "PATH=%s\n", getenv("PATH"));
      
        fprintf(cronjob_hw_file, "*/%s* * * * *  fortune | espeak \n",command->args[0]);
        fclose(cronjob_hw_file);
        command->name = "crontab";
        command->args[0] = "cronjob.txt";
 
  }
  
      redirection_part2(command);

      /// This shows how to do exec with environ (but is not available on MacOs)
      // extern char** environ; // environment variables
      // execvpe(command->name, command->args, environ); // exec+args+path+environ

      /// This shows how to do exec with auto-path resolve
      // add a NULL argument to the end of args, and the name to the beginning
      // as required by exec

      // increase args size by 2
      command->args = (char **)realloc(
          command->args, sizeof(char *) * (command->arg_count += 2));

      // shift everything forward by 1
      for (int i = command->arg_count - 2; i > 0; --i)
        command->args[i] = command->args[i - 1];

      // set args[0] as a copy of name
      command->args[0] = strdup(command->name);
      // set args[arg_count-1] (last) to NULL
      command->args[command->arg_count - 1] = NULL;

      // TODO: do your own exec with path resolving using execv()
      // do so by replacing the execvp call below
      // execvp(command->name, command->args); // exec+args+path
      // PART 1
      // print_command(command);
      char bin_dir[100];
      strcpy(bin_dir, "/usr/bin/"); // copy for bin direction
      strcat(bin_dir, command->name);
      execv(bin_dir, command->args);

      exit(0);
    }
    if (command->next)
    {
      command = command->next;
      continue;
    }
  }
  // TODO: implement background processes here

  for (int j = 0; j < 2 * num_pipes; j++)
  {
    close(fd_pipes[j]);
  }
  for (int k = 0; k < num_pipes + 1; k++)
  { // wait for child process to finish
    wait(&status);
  }
  return SUCCESS;

  printf("-%s: %s: command not found\n", sysname, command->name);
  return UNKNOWN;
}

// TODO: your implementation here
void redirection_part2(struct command_t *command)
{
  // Dont put spaces after redirection symbols !!!!
  if (command->redirects[0] != NULL)
  { // for <
    int in = open(command->redirects[0], O_RDONLY, 0644);
    if (in == -1)
    {
      printf("Error opening redirect source file!\n");
    }
    dup2(in, STDIN_FILENO); // copy file to stdin
    close(in);
  }

  if (command->redirects[1] != NULL)
  { // for >
    int out = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out == -1)
    {
      printf("Error opening redirect target file!\n");
    }
    dup2(out, STDOUT_FILENO); // copy file to stdout
    close(out);
  }

  if (command->redirects[2] != NULL)
  { // for >>
    int out = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (out == -1)
    {
      printf("Error opening redirect target file!\n");
    }
    dup2(out, STDOUT_FILENO); // copy file to stdout
    close(out);
  }
}

int chatroom(struct command_t *command)
{
  char chatroom_dir[100];
  strcpy(chatroom_dir, "/tmp");
  // create tmp folder if doesnt exist
  if (mkdir(chatroom_dir, S_IRWXU | S_IRWXG | S_IRWXO) == -1)
  {
    if (errno != EEXIST)
    {
      printf("Chatroom error tmp folder: %s\n", strerror(errno));
    }
  }
  strcat(chatroom_dir, "/chatroom-");
  strcat(chatroom_dir, command->args[0]);
  strcat(chatroom_dir, "/");

  // create room folder if doesnt exist
  if (mkdir(chatroom_dir, S_IRWXU | S_IRWXG | S_IRWXO) == -1)
  {
    if (errno != EEXIST)
    {
      printf("Chatroom error room folder: %s\n", strerror(errno));
    }
  }

  char pipe_path[100];
  // create user named pipe if doesnt exist
  strcpy(pipe_path, chatroom_dir);
  strcat(pipe_path, command->args[1]);

  int fd;
  char buff_in[140];
  char buff_out[140];

  mkfifo(pipe_path, 0666);
  if (errno != EEXIST)
  {
    printf("Chatroom error named pipe: %s\n", strerror(errno));
  }

  printf("Welcome to %s!\n", command->args[0]);
  pid_t pid = fork();
  if (pid == 0)
  {
    while (1)
    {
      printf("[%s] %s >", command->args[0], command->args[1]);

      // get user message
      fgets(buff_in, 140, stdin);

      char temp_buff[180];
      strcpy(temp_buff, "\n[");
      strcat(temp_buff, command->args[0]);
      strcat(temp_buff, "] ");
      strcat(temp_buff, command->args[1]);
      strcat(temp_buff, ": ");
      strcat(temp_buff, buff_in);

      DIR *d;
      struct dirent *dir;
      d = opendir(chatroom_dir);
      char *write_pipe = (char *)malloc(1 * sizeof(char));
      write_pipe[0] = '\0';
      char *temp = (char *)malloc((strlen(write_pipe) + 1) * sizeof(char));
      if (d)
      {
        while ((dir = readdir(d)) != NULL)
        {

          if ((strcmp(dir->d_name, command->args[1]) != 0) && dir->d_name[0] != '.')
          {
            // each time keep the room directory, append the username
            temp = (char *)realloc(temp, (strlen(chatroom_dir) + strlen(dir->d_name) + 1) * sizeof(char));
            strcat(temp, chatroom_dir);
            strcat(temp, dir->d_name);
            //printf("%s \n", temp);

            fd = open(temp, O_WRONLY);
            // write to other pipes
            write(fd, temp_buff, strlen(temp_buff) + 1);

            close(fd);
            temp[0] = '\0';
          }
        }
        closedir(d);
      }
    }
  }
  else
  {
    while (1)
    {
      // read from YOUR pipe only
      fd = open(pipe_path, O_RDONLY);
      read(fd, buff_out, sizeof(buff_out));
      printf("%s", buff_out);
      close(fd);
    }
  }
}

int motivation_prompt(int cycle_no, int max_cycle)
{
  if (cycle_no == 1)
  {
    printf("Good luck working, keep it zen ⊹╰(⌣ʟ⌣)╯⊹\n");
  }
  if (cycle_no == max_cycle)
  {
    printf("Last cycle! You got this ᕙ(⌣◡⌣”)ᕗ\n");
  }
}

int pomodoro(struct command_t *command)
{
  int num_cycles = atoi(command->args[0]);
  for (int i = 1; i <= num_cycles; i++)
  {
    printf("\aEntering pomodoro cycle %d ... ", i);

    motivation_prompt(i, num_cycles);

    pid_t pid = fork();
    if (pid == 0)
    {
      int study_min = atoi(command->args[1]);
      // printf("study ID: %d\n",getpid());
      if ((study_min - 5) > 0)
      {
        sleep((study_min - 5) * 60);
        printf("Last five minutes... little goes a long way!\n");
        sleep(5 * 60);
      }
      else
      {
        sleep(study_min * 60);
      }

      exit(0);
    }
    else
    {
      wait(0);
      if (i == num_cycles)
      {
        printf("Great Job! You completed ALL your cycles ( ⌒o⌒)人(⌒-⌒\n");
      }
      else
      {
        printf("Nice... You completed %d cycle(s) \n", i);
        // printf("break ID: %d\n",getpid());
        printf("\aNow its time for a %s minute break...\n", command->args[2]);
        int break_mins = atoi(command->args[2]);
        sleep(break_mins * 60);
      }
    }
  }
}

int fib(int n)
{
  if (n <= 1)
    return n;
  return fib(n - 1) + fib(n - 2); // recursive function call
}

void fibonacci_game(int arr[])
{
  printf("\n\nWelcome to the game of Fibonacci!\n\n");
  int n = (rand() % GAME_ARRAY_SIZE); // randomly assign n

  int guess1;
  int guess2;
  int num_user = 2;
  int fibonacci = fib(n); // calling fib function
  printf("Guess the %d th term\n", n);

  printf("Number of users %d\n", num_user); // set to 2

  printf("Guess of user 1:");
  fflush(stdout);
  scanf("%d", &guess1);
  printf("Guess of user 2:");
  fflush(stdout);
  scanf("%d", &guess2);

  if (abs(fibonacci - guess1) < abs(fibonacci - guess2))
  {
    printf("Congratulations User 1!\n");
  }
  else if (abs(fibonacci - guess1) > abs(fibonacci - guess2))
  {
    printf("Congratulations User 2!\n");
  }
  else
  {
    printf("Congratulations User 1 and User 2! You both win:))\n");
  }
  printf("The value of fib(%d) is %d", n, fibonacci);
  printf("\n");
}
