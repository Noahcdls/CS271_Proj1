#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd, portno, n;//socket and port number
    struct sockaddr_in serv_addr;//ip addr
    struct hostent *server;//struct for host info

    char buffer[256];
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);//need ip addr and port
       exit(0);
    }
    portno = atoi(argv[2]);//convert port to int
    sockfd = socket(AF_INET, SOCK_STREAM, 0);//get socket file descriptor us TCP standard
    if (sockfd < 0) //failure
        error("ERROR opening socket");
    server = gethostbyname(argv[1]);//get host name
    if (server == NULL) {//failure
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    printf("GOT SERVER NAME\n");
    bzero((char *) &serv_addr, sizeof(serv_addr));//zero out server address with given size
    serv_addr.sin_family = AF_INET;//TCP
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);//copy address
    serv_addr.sin_port = htons(portno);
    //printf("h_addr: %s\n", inet_ntoa(serv_addr.sin_addr));
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");//connect or FAILURE
    while(1){
    printf("Please enter the message: ");//get a message and zero out buffer and get from stdin
    bzero(buffer,256);
    fgets(buffer,255,stdin);
    n = write(sockfd,buffer,strlen(buffer));//write to socket and send
    if (n < 0) 
         error("ERROR writing to socket");
    bzero(buffer,256);
    n = read(sockfd,buffer,255);//wait for read message
    if (n < 0) //failure message from read
         error("ERROR reading from socket");
    printf("%s\n",buffer);//print the message
    }
    close(sockfd);//close socket
    return 0;
}