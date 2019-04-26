#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>

#define true 1
#define false 0
#define bool int

int peek();
void push(int data);
void pop();
int empty();
void display();
void create();
void stack_size();
bool starts_with(const char *pre, const char *str);

int s_size = 0;
int stack[256];

int main() 
{
    int fds[2]; // 0 for input, 1 for output
    pipe(fds);
    
    pid_t parent_pid = getpid(); 	
    pid_t child_pid = fork();

    if (child_pid == 0) 
    {
        // Code of child (server)
        close(fds[1]);

        create(); // Create initial empty stack

        while (true)
        {
            if (raise(SIGSTOP) != 0)
            {
                return EXIT_FAILURE;
            }

            int parameters[2];
            read(fds[0], parameters, sizeof(parameters));

            switch(parameters[0])
            {
                case 0:
                    printf("[SERVER]: Command PEEK received.\n");
                    if (s_size > 0)
                    {
                        printf("[SERVER]: Top element of the stack is %d\n", peek());
                    } else 
                    {
                        printf("[SERVER]: The stack is empty!\n");
                    }
                    break;
                case 1:
                    printf("[SERVER]: Command PUSH received with parameter %d\n", parameters[1]);
                    push(parameters[1]);
                    printf("[SERVER]: Now the top element is %d\n", peek());
                    break;
                case 2:
                    printf("[SERVER]: Command DISPLAY received.\n");
                    if (s_size > 0)
                    {
                        display();
                    } else
                    {
                        printf("[SERVER]: The stack is empty!\n");
                    }
                    break;
                case 3:
                    printf("[SERVER]: Command POP received.\n");
                    if (s_size > 0)
                    {
                        pop();
                    } else
                    {
                        printf("[SERVER]: The stack is empty!\n");
                    }
                    break;
                case 4:
                    printf("[SERVER]: Command STACKSIZE is received.\n");
                    stack_size();
                    break;
                case 5:
                    printf("[SERVER]: Command EMPTY is received.\n");
                    printf("Is stack empty? ");
                    printf(empty() == true ? "true\n" : "false\n");
                    break;
                case 6:
                    printf("[SERVER]: Command CREATE is recieved.\n");
                    create();
                    printf("[SERVER]: The stack was recreated.\n");
            }

            sleep(1);    
        }

    } else 
    {
        // Code of parent (client)
        close(fds[0]);

        printf("Child process PID: %d\n", child_pid);
        printf("Parent process PID: %d\n", parent_pid);
        printf("Type 'help' of '?' for list of commands.\n");

        while (true) 
        {
            printf("Enter a command: ?- ");

            char c_data[256];
            fgets(c_data, sizeof(c_data), stdin);

            int parameters[2];

            if (strcmp(c_data, "peek\n") == 0)
            {
                // Peek command
                parameters[0] = 0;
            } else if(starts_with("push", c_data))
            {
                // Push command
                int param = 0;
                int i = 5;
                while (c_data[i] != '\n')
                {
                    param *= 10;
                    param += c_data[i] - '0';
                    i++;
                }
                parameters[0] = 1;
                parameters[1] = param;
            }else if (strcmp(c_data, "display\n") == 0)
            {
                // Display command
                parameters[0] = 2;
            } else if (strcmp(c_data, "pop\n") == 0) 
            {
                // Pop command
                parameters[0] = 3;
            } else if (strcmp(c_data, "?\n") == 0 || strcmp(c_data, "help\n") == 0) 
            {
                // Help command
                printf("-----------------\n");
                printf("push N\npop\ndisplay\npeek\nempty\nstacksize\ncreate\nexit\n?\nhelp\n"); 
                printf("-----------------\n");
            } else if (strcmp(c_data, "stacksize\n") == 0) 
            {   
                // Stacksize command
                parameters[0] = 4;
            } else if (strcmp(c_data, "empty\n") == 0) 
            {
                // Empty command
                parameters[0] = 5;
            } else if (strcmp(c_data, "create\n") == 0) 
            {
                // Create command
                parameters[0] = 6;
            } else if (strcmp(c_data, "exit\n") == 0)
            {
                // Exit command
                printf("Termination...\n");
                kill(child_pid, SIGTERM);
                break;
            }

            write(fds[1], parameters, sizeof(parameters)); 
            kill(child_pid, SIGCONT);

            sleep(1);
        }
    }
    return EXIT_SUCCESS;
}
 
void create()
{
    s_size = 0;
    for (int i = 0; i < 256; ++i)
    {
        stack[i] = 0;
    }
}

int peek()
{
    return stack[s_size - 1]; 
}

void push(int data)
{
    stack[s_size] = data;
    s_size++; 
}

void display()
{
    printf("Stack from top to bottom:\n");
    for (int i = s_size - 1; i >= 0; --i)
    {
        printf("%d ", stack[i]);
    }
    printf("\n");
}

void pop()
{
    stack[s_size - 1] = 0;
    s_size--;
}

int empty()
{
    return s_size == 0;
}

void stack_size()
{
    printf("Size of the stack is ");
    printf("%d.\n", s_size);
}

bool starts_with(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre);
    size_t lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}
