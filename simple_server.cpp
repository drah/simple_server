#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BACKLOG 10
#define MSGBUF_SIZE 1024

enum {HTML, TXT, CSS, GIF, JPG, PNG, BMP, DOC, PDF, MP4, SWF, OGG, BZ2, GZ};

void sigchild(int _){
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

int get_server_info(char port[], struct addrinfo *hints, struct addrinfo **info){
  memset(hints, 0, sizeof(*hints));
  hints->ai_family = AF_UNSPEC;
  hints->ai_socktype = SOCK_STREAM;
  hints->ai_flags = AI_PASSIVE;
  int v = 0;
  if((v = getaddrinfo(NULL, port, hints, info)) != 0){
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(v));
    return -1;
  }
  return 0;
}

int socket_and_bind(int *fd, struct addrinfo *info){
  struct addrinfo *p;
  int yes = 1, good = -1;
  for(p = info; p != NULL; p = p->ai_next){
    if((*fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
      perror("socket error");
      continue;
    }
    //if(setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    //  exit(1);
    if(bind(*fd, p->ai_addr, p->ai_addrlen) == -1){
      perror("bind error");
      close(*fd);
      continue;
    }
    good = 1;
    break;
  }
  return good;
}

int set_signal(struct sigaction *sa){
  sa->sa_handler = sigchild;
  sigemptyset(&(sa->sa_mask));
  sa->sa_flags = SA_RESTART;
  if(sigaction(SIGCHLD, sa, NULL) == -1){
    perror("sigaction error");
    return -1;
  }
  return 0;
}

int parse_method(char *msg){
  if(memcmp(msg, "GET", 3) == 0) return 1;
  if(memcmp(msg, "POST", 4) == 0) return 2;
}

void parse_request_object(char *msg, char *buf){
  char *head = (char*)memchr(msg, ' ', strlen(msg)) + 1;
  char *tail = (char*)memchr(head, ' ', strlen(head)) - 1;
  memcpy(buf, head, tail-head+1);
  buf[tail-head+1] = '\0';
}

bool exists(char *path){
  if(access(path, F_OK) == -1) return false;
  return true;
}
bool is_dir(char *path){
  struct stat path_stat;
  stat(path, &path_stat);
  return S_ISDIR(path_stat.st_mode);
}
bool accessible(char *path){
  if(access(path, R_OK) == 0) return true;
  return false;
}
bool has_slash(char *path){
  return path[strlen(path) - 1] == '/';
}
bool readable(char *path){
  return accessible(path);
}
bool has_html(char *path){
  if(!has_slash(path) && (strlen(path)+2) < MSGBUF_SIZE){
    path[strlen(path)+1] = '\0';
    path[strlen(path)] = '/';
  }
  if(strlen(path)+11 < MSGBUF_SIZE){
    path[strlen(path)+10] = '\0';
    memcpy(path+strlen(path), "index.html", 10);
  }
  else exit(1);
  return exists(path);
}
bool html_readable(char *path){
  return accessible(path);
}

int append_content_type(char *msg, char *path){
  char *p = msg + strlen(msg);
  strcpy(p, "Content-Type: \0");
  p = p + strlen(p);
  char *dot = strrchr(path, '.');
  if(dot == NULL) return -1;
  else if(strcmp(dot+1, "htm") == 0 || strcmp(dot+1, "html") == 0)
    strcpy(p, "text/html\n\0");
  else if(strcmp(dot+1, "txt") == 0)
    strcpy(p, "text/plain\n\0");
  else if(strcmp(dot+1, "gif") == 0)
    strcpy(p, "image/gif\n\0");
  else if(strcmp(dot+1, "jpg") == 0 || strcmp(dot+1, "jpeg") == 0)
    strcpy(p, "image/jpeg\n\0");
  else if(strcmp(dot+1, "png") == 0)
    strcpy(p, "image/png\n\0");
  else if(strcmp(dot+1, "bmp") == 0)
    strcpy(p, "image/x-ms-bmp\n\0");
  else if(strcmp(dot+1, "pdf") == 0)
    strcpy(p, "application/pdf\n\0");
  else if(strcmp(dot+1, "mp4") == 0)
    strcpy(p, "video/mp4\n\0");
  else if(strcmp(dot+1, "ogg") == 0)
    strcpy(p, "audio/ogg\n\0");
  else if(strcmp(dot+1, "css") == 0)
    strcpy(p, "text/css\n\0");
  else if(strcmp(dot+1, "doc") == 0)
    strcpy(p, "application/msword\n\0");
  else if(strcmp(dot+1, "swf") == 0)
    strcpy(p, "application/x-shockwave-flash\n\0");
  else if(strcmp(dot+1, "bz2") == 0)
    strcpy(p, "applicatoin/x-bzip2\n\0");
  else if(strcmp(dot+1, "gz") == 0)
    strcpy(p, "application/x-gzip\n\0");
  else 
    return -2;
  return 0;
}

int ls_length(char *path){
    char *p = strrchr(path, '/');
    p[1] = '\0';
    char cmd[MSGBUF_SIZE];
    sprintf(cmd, "/bin/ls -la %s", path);
    FILE *fp = popen(cmd, "r");
    int c = 0, count = 0;
    while(c = fread(cmd, sizeof(char), MSGBUF_SIZE-1, fp))
      count += c;
    return count;
}

int append_content_length(char *msg, char *path){
  char *p = msg + strlen(msg);
  struct stat path_stat;
  if(stat(path, &path_stat) == 0){
    sprintf(p, "Content-Length: %ld\n", path_stat.st_size);
  }
  else {
    sprintf(p, "Content-Length: %d\n", ls_length(path));
  }
  return 0;
}

int append_connection_info(char *msg){
  char *p = msg + strlen(msg);
  sprintf(p, "Connection: keep-alive\nKeep-Alive: timeout=100\n\n");
  return 0;
}
int send_content(int fd, char *path){
  char msg[MSGBUF_SIZE];
  if(path[strlen(path)-1] == '/'){
    char *p = path + strlen(path);
    sprintf(p, "index.html");
  }
  if(exists(path)){
    int filedes = open(path, O_RDONLY);
    int c = 0;
    while(c = read(filedes, msg, MSGBUF_SIZE-1)){
      msg[c] = '\0';
      send(fd, msg, c, 0);
    }
    close(filedes);
  }
  else {
    char *p = strrchr(path, '/');
    p[1] = '\0';
    char cmd[MSGBUF_SIZE];
    sprintf(cmd, "/bin/ls -la %s", path);
    FILE *fp = popen(cmd, "r");
    int c = 0;
    while(c = fread(msg, sizeof(char), MSGBUF_SIZE-1, fp)){
      send(fd, msg, c, 0);
    }
  }
}

void send_handler(int state_code, int fd, char *path){
  char msg[MSGBUF_SIZE];
  memset(msg, 0, MSGBUF_SIZE);
  switch(state_code){
    case 404:
      strcpy(msg, "HTTP/1.1 404 Not Found\nContent-Type: text/plain\nContent_Length: 14\n\n404 Not Found\0");
      send(fd, msg, strlen(msg), 0);
      return;
    case 403:
      strcpy(msg, "HTTP/1.1 403 Forbidden\nContent-Type: text/plain\nContent_Length: 14\n\n403 Forbidden\0");
      send(fd, msg, strlen(msg), 0);
      return;
    case 301:
      strcpy(msg, "HTTP/1.1 301 Moved Permanently\n\0");
      break;
    case 200:
      strcpy(msg, "HTTP/1.1 200 OK\n\0");
      break;
    default:
      exit(1);
  }
  if(append_content_type(msg, path) != 0){
    fprintf(stderr, "Append_content_type error\n");
    exit(1);
  }
  append_content_length(msg, path);
  append_connection_info(msg);
  send(fd, msg, strlen(msg), 0);
  printf("sent back: \n%s", msg);
  memset(msg, 0, sizeof(msg));
  send_content(fd, path);
}

void request_handler(int fd, char docroot[]){
  char msgbuf[MSGBUF_SIZE];
  memset(msgbuf, 0, MSGBUF_SIZE);
  recv(fd, msgbuf, MSGBUF_SIZE-1, 0);
  printf("%s\n", msgbuf);
  //int method = parse_method(msgbuf);
  //method == GET in this project
  char object[MSGBUF_SIZE];
  //memcpy(object, docroot, strlen(docroot));
  parse_request_object(msgbuf, object); 
  char path[MSGBUF_SIZE];
  sprintf(path, "%s%s", docroot, object);
  
  bool stat301 = false;
  if(!exists(path))
    send_handler(403, fd, path);
  else if(!is_dir(path)){
    if(!accessible(path))
      send_handler(404, fd, path);
    else
      send_handler(200, fd, path);
  }
  else {
    if(!has_slash(path))
      stat301 = true;
    if(!readable(path))
      send_handler(404, fd, path);
    else if(!has_html(path)){
      if(stat301)
        send_handler(301, fd, path);
      else
        send_handler(200, fd, path);
    }
    else {
      if(!html_readable(path))
        send_handler(403, fd, path);
      else {
        if(stat301)
          send_handler(301, fd, path);
        else
          send_handler(200, fd, path);
      }
    }
  }
}

int main(int argc, char *argv[]){
  if(argc != 3){
    fprintf(stderr, "Usage: ./webserver port \"/path/to/docroot\"\n");
    return 2;
  }
  char port[6];
  strcpy(port, argv[1]);
  //set the dir
  
  struct addrinfo hints, *server_info;
  if(get_server_info(port, &hints, &server_info) == -1)
    return 1;
  
  int sfd;
  if(socket_and_bind(&sfd, server_info) == -1)
    return 1;
  freeaddrinfo(server_info);
  
  if(listen(sfd, BACKLOG) == -1)
    return 1;
  
  struct sigaction sa;
  if(set_signal(&sa) == -1)
    return 1;
  
  while(true){
    struct sockaddr_storage cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);
    int new_fd;
    if((new_fd = accept(sfd, (struct sockaddr*)&cli_addr, &cli_addr_len)) == -1){
      perror("accept error");
      continue;
    }
    if(fork() == 0){
      close(sfd);
      request_handler(new_fd, argv[2]);
      close(new_fd);
      exit(0);
    }
    close(new_fd);
  }

  return 0;
}
