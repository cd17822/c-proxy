/*--------------------------------------------------------------------*/
#define _BSD_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <netdb.h>
#include <time.h> 
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>


#define MAX_MSG_LEN 65536
#define MAX_NAME_LEN 256
#define CACHE_SIZE 256
/*--------------------------------------------------------------------*/
int sendResponse(int sd, char* msg, int contentLength);
int interactWithHost(int sd, char* ip, char* buf);
pthread_mutex_t MUTEX = PTHREAD_MUTEX_INITIALIZER;
int FIRST_FREE_CACHE_INDEX = 0;
char CACHE_URLS[CACHE_SIZE][MAX_NAME_LEN];
char CACHE[CACHE_SIZE][MAX_MSG_LEN];
int CACHE_LENGTHS[CACHE_SIZE];
/*----------------------------------------------------------------*/
/* prepare server to accept requests
   returns file descriptor of socket
   returns -1 on error
*/
int startserver() {
  int sd; /* socket descriptor */

  char* servhost = malloc(MAX_NAME_LEN);
  unsigned short servport; /* port assigned to this server */

  /* create a TCP socket using socket() */
  sd = socket(AF_INET, SOCK_STREAM, 0);
  if (sd < 0){
    perror("Error on socket: ");
    exit(1);
  }

  /* bind the socket to some port using bind(); let the system choose a port */
  struct sockaddr_in sa;
  socklen_t addr_size = sizeof sa;

  sa.sin_family = AF_INET;
  sa.sin_port = htons(0);
  sa.sin_addr.s_addr = INADDR_ANY;

  if (bind(sd, (struct sockaddr*)&sa, addr_size) < 0){
    perror("Error on bind: ");
    exit(1);
  }

  /* we are ready to receive connections */
  if (listen(sd, 5) < 0) {
    perror("Error on listen: ");
    exit(1);
  }

  /*
   figure out the full host name (servhost)
   use gethostname() and gethostbyname()
   full host name is remote**.cs.binghamton.edu
   */
  if (gethostname(servhost, MAX_NAME_LEN) < 0){
    perror("Error on gethostname: ");
    exit(1);
  }

  struct hostent* host = gethostbyname(servhost);
  // strcpy(servhost, host->h_name);

  /*
   figure out the port assigned to this server (servport)
   use getsockname()
   */
  if (getsockname(sd, (struct sockaddr*)&sa, &addr_size) < 0) {
    perror("Error on getsockname: ");
    exit(1);
  }

  servport = ntohs(sa.sin_port);

  /* ready to accept requests */
  printf("admin: started server on '%s' at '%hu'\n", servhost, servport);

  free(servhost);
  close(servport);

  return sd;
}

int readn(int sd, char* ip, char* buf, int maxResponseSize) {
  int total = maxResponseSize;
  int received = 0;
  int received_this_time = 0;

  do {
    received_this_time = read(sd, buf + received, total - received);

    if (received_this_time < 0) {
      perror("ERROR reading response from client");
    }

    received += received_this_time;
    // printf("readn loop received: %d\n", received);
  } while (0 && received_this_time != 0);

  if (received == maxResponseSize) {
    perror("ERROR storing complete response from client");
    buf[maxResponseSize - 1] = '\0';    
  }

  if (!interactWithHost(sd, ip, buf)) {
    return 0;
  } else {
    printf("ERROR QUERYING HOST\n");
    return 1;
  }

  return 1;
}

// returns the of the string that the endpoint starts at
int getEndpointIndexFrom(char* string) {
  int host_index;
  size_t host_length;

  for (int i = 0; i < strlen(string)-3; ++i) {
    if (strncmp(&string[i], "Host: ", strlen("Host: ")) == 0) {
      host_index = i + strlen("Host: ");
      for (int j = 0; j < strlen(string) - i - strlen("Host: "); ++j) {
        if (string[i+strlen("Host: ")+j] == '\r' || string[i+strlen("Host: ")+j] == '\n') {
          host_length = j;
          break;
        }
      }
      break;
    }
  }

  for (int i = 0; i < strlen(string)-host_length; ++i) {
    if (strncmp(&string[i], &string[host_index], host_length) == 0) {
      return i + host_length - 1;
    }
  }

  return -1;
}

void buildHostnameEndpointAndMessage(char* buf, char* hostName, char* endpoint, char* message) {
  // build first line tokens
  char method[strlen(buf)];
  char protocol[strlen(buf)];
  sscanf(buf, "%s %s %s", method, hostName, protocol);
  // printf("method: %s\n", method);
  // printf("hostName: %s\n", hostName);
  // printf("protocol: %s\n", protocol);

  // build endpoint
  int endpoint_index = getEndpointIndexFrom(buf);
  int a = -1;
  for (int i = endpoint_index - strlen(method); i < strlen(hostName); ++i) {
    endpoint[++a] = hostName[i];
  }
  endpoint[++a] = '\0';
  hostName[endpoint_index - strlen(method)] = '\0';

  // build message
  char line1[strlen(buf)];
  sprintf(line1, "%s %s %s", method, endpoint, protocol);
  // printf("hostName: %s\n", protocol);
  // printf("line1: %s\n", line1);
  char* rest = &buf[strlen(line1) + strlen(hostName)];
  sprintf(message, "%s%s%s", line1, rest, "\0");
}

int parsePortNoFromEndpoint(char* endpoint) {
  int port_no = 80;
  if (endpoint[0] == ':') {
    char buf[5];
    int i = 0;
    while (endpoint[i] != '/' && endpoint[i] != '\0') {
      buf[i] = endpoint[i];
      ++i;
    }
    port_no = atoi(buf+1); // dont want to add ':'
    endpoint = &endpoint[i];
  } else if (endpoint[0] != '/' && endpoint[0] != '\0') {
    printf("NON-CONFORMING ENDPOINT: %s (endpoint[0] = %d)\n", endpoint, endpoint[0]);
  }

  return port_no;
}

void getHostNameSansPort(char* dest, char* hostName) {
  int i;
  for (i = 0; i < strlen(hostName); ++i) {
    if (hostName[i] == ':') {
      break;
    }

    dest[i] = hostName[i];
  }

  dest[i] = '\0';
}

int connectServerSocket(char* hostName, int port_no) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) perror("ERROR opening socket");

  // printf("host_name: %s\n", hostName);
  char host_name_sans_port[strlen(hostName)];
  getHostNameSansPort(host_name_sans_port, hostName);

  /* lookup the ip address */  
  struct hostent* server = gethostbyname(host_name_sans_port);
  if (server == NULL) {
    // printf("host_name_sans_port: %s\n", host_name_sans_port);
    perror("ERROR no such host");
    return -1;
  }

  /* fill in the structure */
  struct sockaddr_in serv_addr;
  memset(&serv_addr,0,sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port_no);
  memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

  /* connect the socket */
  if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
    perror("ERROR connecting");
    return -1;
  }

  return sockfd;
}

int queryServer(int sockfd, char* message) {
  int msg_len = strlen(message);
  int sent = 0;
  int sent_this_time = 0;

  while (sent < msg_len) {
    sent_this_time = write(sockfd, message+sent, msg_len-sent);
    
    if (sent_this_time < 0) {
      perror("ERROR writing message to socket");
      return -1;
    } else if (sent_this_time == 0) {
      break;
    }
    
    sent += sent_this_time;
  }

  return 0;
}

int getServerResponse(int sockfd, char* response, int maxResponseSize) {
  int total = maxResponseSize;
  int received = 0;
  int received_this_time = 0;

  do {
    received_this_time = read(sockfd, response + received, total - received);
    
    if (received_this_time < 0) {
      perror("ERROR reading response from socket");
      break;
    }

    received += received_this_time;
    // printf("gsr loop received: %d\n", received);
  } while (received_this_time != 0);

  if (received == maxResponseSize) {
    perror("ERROR storing complete response from socket");
  }

  return received;
}

int sendResponse(int sd, char* msg, int contentLength) {
  /* write message text */
  if (contentLength > 0) {
    write(sd, msg, contentLength); // DOES THIS HAVE TO LOOP
    return 0;
  }

  return 1;
}

int cacheIndexOf(char* url) {
  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (!strcmp(url, CACHE_URLS[i])) {
      return i;
    }
  }
  return -1;
}

void insertIntoCache(char* url, char* response, int contentLength) {
  pthread_mutex_lock(&MUTEX);

  strcpy(CACHE_URLS[FIRST_FREE_CACHE_INDEX], url);
  strcpy(CACHE[FIRST_FREE_CACHE_INDEX], response);
  CACHE_LENGTHS[FIRST_FREE_CACHE_INDEX] = contentLength;
  FIRST_FREE_CACHE_INDEX = (FIRST_FREE_CACHE_INDEX + 1) % CACHE_SIZE;

  pthread_mutex_unlock(&MUTEX);
}

int interactWithHost(int sd, char* ip, char* buf) {
  // printf("Client to Proxy:\n----------------------\n%s\n", buf);

  // get some interesting tokens from the request
  char host_name[strlen(buf)];
  char endpoint[strlen(buf)];
  char message[strlen(buf)];
  buildHostnameEndpointAndMessage(buf, host_name, endpoint, message);
  // printf("message:\n%s\n", message);
  // printf("host_name: %s\n", host_name);
  // printf("endpoint: %s\n", endpoint);
  char url[strlen(host_name) + strlen(endpoint)];
  sprintf(url, "%s%s", host_name, endpoint);

  int content_length;
  int max_response_size = MAX_MSG_LEN;
  char response[max_response_size];
  
  struct timeval tv1, tv2;
  gettimeofday(&tv1, NULL);

  int cache_index = cacheIndexOf(url);
  if (cache_index != -1) {
    strcpy(response, CACHE[cache_index]);
    content_length = CACHE_LENGTHS[cache_index];
  } else {
    /* create the socket */
    int sockfd = connectServerSocket(host_name + strlen("http://"), parsePortNoFromEndpoint(endpoint));

    /* send the request */
    // printf("Proxy to Server:\n----------------------\n%s\n", message);
    queryServer(sockfd, message);

    /* receive the response */
    // printf("Server to Proxy (also Proxy to Client):\n----------------------\n%s\n", response);
    content_length = getServerResponse(sockfd, response, max_response_size);  

    insertIntoCache(url, response, content_length);

    /* close the socket */
    close(sockfd);
  }

  gettimeofday(&tv2, NULL);
  int msec = (tv2.tv_usec - tv1.tv_usec) / 1000 + (tv2.tv_sec - tv1.tv_sec) * 1000;
  // printf("endpoint: %s\n", endpoint);
  // printf("url: %s\n", url);
  printf("%s|%s|%s|%d|%d\n", ip, url, (cache_index == -1) ? "CACHE_MISS" : "CACHE_HIT", content_length, msec);

  /* send response back to client */
  // printf("response:\n%s\n", response);
  sendResponse(sd, response, content_length);

  return 0;
}
