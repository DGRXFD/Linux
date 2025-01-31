#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include "threadpool.h"

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define THREADNUM 500 /*number of threads in thread pool*/

#ifndef SIGCLD
# define SIGCLD SIGCHLD
#endif

struct 
{
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" }, 
	{"jpeg","image/jpeg"}, 
	{"png", "image/png" }, 
	{"ico", "image/ico" }, 
	{"zip", "image/zip" }, 
	{"gz", "image/gz" }, 
	{"tar", "image/tar" }, 
	{"htm", "text/html" }, 
	{"html","text/html" }, 
	{0,0} };

typedef struct 
{ 
	int hit;
	int fd;
} webparam;

unsigned long get_file_size(const char *path) 
{
	unsigned long filesize = -1; 
	struct stat statbuff; 
	if(stat(path, &statbuff) < 0)
		return filesize; 
	else
		filesize = statbuff.st_size; 
	return filesize; 
}

void logger(int type, char *s1, char *s2, int socket_fd) 
{
	int fd ;
	char logbuffer[BUFSIZE*2];
	switch (type) 
	{
		case ERROR: 
			(void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2,errno,getpid());
			break;
		case FORBIDDEN:
			(void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection:close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
			break;
		case NOTFOUND:
			(void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
			break;
		case LOG: 
			(void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); 
			break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) 
	{
		(void)write(fd,logbuffer,strlen(logbuffer)); (void)write(fd,"\n",1);
		(void)close(fd);
	}
	//if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

/* this is a web thread, so we can exit on errors */
void web(void * data)
{
	int fd;
	int hit;

	int j, file_fd, buflen;
	long i, ret, len;
	char * fstr;
	char buffer[BUFSIZE+1]; /* static so zero filled */
	webparam *param = (webparam*)data; 
	fd=param->fd;
	hit=param->hit;

	ret =read(fd,buffer,BUFSIZE); /* read web request in one go */
	if(ret == 0 || ret == -1)  /* read failure stop now */
		logger(FORBIDDEN,"failed to read browser request","",fd);
	else
	{
		if(ret > 0 && ret < BUFSIZE) /* return code is valid chars */
			buffer[ret]=0; /* terminate the buffer */
		else buffer[0]=0;
		for(i=0;i<ret;i++) /* remove cf and lf characters */
			if(buffer[i] == '\r' || buffer[i] == '\n')
				buffer[i]='*';
		logger(LOG,"request",buffer,hit);
		if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) )
			logger(FORBIDDEN,"only simple GET operation supported",buffer,fd);
		for(i=4;i<BUFSIZE;i++)
		{ /* null terminate after the second space to ignore extra stuff */
			if(buffer[i] == ' ')
			{ /* string is "get url " +lots of other stuff */
				buffer[i] = 0;
				break;
			}
		}
		for(j=0;j<i-1;j++) /* check for illegal parent directory use .. */
		if(buffer[j] == '.' && buffer[j+1] == '.')
			logger(FORBIDDEN,"parent directory (..) path names not supported",buffer,fd);
		if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
			(void)strcpy(buffer,"GET /index.html");
		/* work out the file type and check we support it */
		buflen=strlen(buffer);
		fstr = (char *)0;
		for(i=0;extensions[i].ext != 0;i++)
		{
			len = strlen(extensions[i].ext);
			if( !strncmp(&buffer[buflen-len], extensions[i].ext, len))
			{
				fstr =extensions[i].filetype;
				break;
			}
		}
		if(fstr == 0)
			logger(FORBIDDEN,"file extension type not supported",buffer,fd);
		if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) /* open the file for reading */ 
			logger(NOTFOUND, "failed to open file",&buffer[5],fd);
		logger(LOG,"SEND",&buffer[5],hit);
		len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* 使用 lseek 来获得文件长度，比较低效*/
		(void)lseek(file_fd, (off_t)0, SEEK_SET); /* 想想还有什么方法来获取*/
 		(void)sprintf(buffer,"HTTP/1.1 200 ok\nserver: nweb/%d.0\ncontent-length: %ld\nconnection: close\ncontent-type: %s\n\n", VERSION, len, fstr); /* header + a blank line */
   		logger(LOG,"Header",buffer,hit); (void)write(fd,buffer,strlen(buffer));
		/* send file in 8kb block - last block may be smaller */
		while ( (ret = read(file_fd, buffer, BUFSIZE)) > 0 )
			(void)write(fd,buffer,ret);
		usleep(10000); /*在 socket 通道关闭前，留出一段信息发送的时间*/
		close(file_fd); 
	}
	close(fd);
	free(param); /*释放内存*/
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */
	if( argc < 3 || argc > 3 || !strcmp(argv[1], "-?") )
	{
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
					"\tnweb is a small and very safe mini web server\n"
					"\tnweb only servers out file/web pages with extensions named below\n" "\t and only from the named directory or its sub-directories.\n"
					"\tThere is no fancy features = safe and secure.\n\n"
					"\tExample: nweb 8181 /home/nwebdir &\n\n"
					"\tOnly Supports:", VERSION);
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);
		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
						"\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
						"\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n" );
		exit(0);
	}
	if( !strncmp(argv[2],"/" ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
		!strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
		!strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
		!strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) )
	{
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
		exit(3);
	}
	if(chdir(argv[2]) == -1)
	{
		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
		exit(4);
	}
	/* Become deamon + unstopable and no zombies children (= no wait()) */
	if(fork() != 0)
		return 0; /* parent returns OK to shell */
	(void)signal(SIGCLD, SIG_IGN); /* ignore child death */
	(void)signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
	for(i=0;i<32;i++)
		(void)close(i); /* close open files */
	(void)setpgrp(); /* break away from process group */
	logger(LOG,"nweb starting",argv[1],getpid());
	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		logger(ERROR, "system call","socket",0);
	port = atoi(argv[1]);
	if(port < 0 || port >60000)
		logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		logger(ERROR,"system call","bind",0);
	if( listen(listenfd,64) <0)
		logger(ERROR,"system call","listen",0);
	/*create a thread pool*/
	threadpool* pool = initThreadPool(THREADNUM);
	for(hit=1; ;hit++)
	{
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR,"system call","accept",0);
		webparam *param=malloc(sizeof(webparam));
		param->hit=hit;
		param->fd=socketfd;
		task* curtask = (struct task*)malloc(sizeof(struct task*));
		curtask->next = NULL;
		curtask->function = &web;
		curtask->arg = (void*)param;
		addTask2ThreadPool(pool, curtask);
	}
	destroyThreadPool(pool);
}