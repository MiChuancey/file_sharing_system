#ifndef __HTTP_CONN_H__
#define __HTTP_CONN_H__ 

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/wait.h>


#define MIN(a,b) ((a)<(b)?(a):(b))
#define exitErr(func) {perror(func);exit(EXIT_FAILURE);}
#define READ_BUFFSIZE 2048
#define WRITE_BUFFSIZE 2048
#define FILE_NAME_LEN 1024
#define USER_NAME_LEN 1024
#define PATH_NAME_LEN 4096

extern const char* ok_200_titile;
extern const char* error_400_titile;
extern const char* error_400_form;
extern const char* error_403_titile;
extern const char* error_403_form;
extern const char* error_404_titile;
extern const char* error_404_form;
extern const char* error_500_titile;
extern const char* error_500_form;

extern const char *doc_root;

typedef enum{
    GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATCH
}METHOD;

typedef enum{
    CHECK_STATE_REQUESTLINE=0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
}CHECK_STATE;//有限状态机状态码

typedef enum{
    NO_REQUEST,GET_REQUEST,POST_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,HTML_REQUEST,
    INTERNAL_ERROR,CLOSED_CONNECTION
}HTTP_CODE;//处理http请求可能的状态码

typedef enum{
    LINE_OK=0,LINE_BAD,LINE_OPEN
}LINE_STATUS;//行的读取状态

typedef enum{
    TEXT_HTML,APPLICATION_OCTET_STREAM
}CONTENT_TYPE;

typedef struct {
    int sock_fd;//
    int epollfd;

    char m_read_buf[READ_BUFFSIZE];
    int m_read_idx;
    int m_check_idx;
    int m_start_line;

    CHECK_STATE check_state;
    char* m_url;
    METHOD m_method;
    char* m_http_version;
    char* m_content_type;
    char* m_form_data;

    int login_state;
    char username[USER_NAME_LEN];
    char* m_post_args[10];
    char* m_get_params;

    int m_content_length;
    int m_linger;
    char* m_host;
    
    //响应部分
    char m_write_buf[WRITE_BUFFSIZE];
    char m_real_file[FILE_NAME_LEN];
    char* m_file_address; //mmap地址
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int m_write_idx;
    
}http_request_t;

typedef struct{

}http_response_t;

void init_http_request(int sockfd,int epollfd,http_request_t* request,int backup);
void parse_http_request(void *);

int write_sock(http_request_t*);

void close_conn(http_request_t*);
 
void process(void*);
HTTP_CODE do_request(http_request_t*);

#endif
