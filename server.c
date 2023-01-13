#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    if (argc < 2)
    {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    bzero((char *)&serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *)&serv_addr,
             sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    int pid;
    while(1){
    newsockfd = accept(sockfd,
                       (struct sockaddr *)&cli_addr,
                       &clilen);//accept a connection
    if (newsockfd < 0)
        error("ERROR on accept");
    pid = fork();

    if(pid == 0){//child process runs forever sending messages
        close(sockfd);
        while (1)
        {
            bzero(buffer, 256);
            n = read(newsockfd, buffer, 255);
            if (n < 0)
                error("ERROR reading from socket");
            else if(n == 0){
                printf("Client %i has left\n", newsockfd);
                close(newsockfd);
                return 0;
            }
            printf("Here is the message: %s\n", buffer);
            char messageback[100];
            sprintf(messageback, "I got your message, client %i\n", newsockfd); 
            n = write(newsockfd, messageback, strlen(messageback));
            if (n < 0){
                close(newsockfd);
                error("ERROR writing to socket");
                return 0;
            }
            
        }
    }
    }
    close(sockfd);
    return 0;
}