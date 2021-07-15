#ifndef __HTTP_CONN_H__
#define __HTTP_CONN_H__ 

#include <sys/types.h>
#include <sys/socket.h>

#define MIN(a,b) ((a)<(b)?(a):(b))
#define READ_BUFFSIZE 2048
#define WRITE_BUFFSIZE 1024

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
    NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,
    INTERNAL_ERROR,CLOSED_CONNECTION
}HTTP_CODE;//处理http请求可能的状态码

typedef enum{
    LINE_OK=0,LINE_BAD,LINE_OPEN
}LINE_STATUS;//行的读取状态

typedef struct {
    int sock_fd;//
    int epollfd;

    char m_read_buf[READ_BUFFSIZE];
    int m_read_idx;
    int m_check_idx;
    int m_start_line;

    CHECK_STATE check_state;
    char* m_url;
    char* m_method;
    char* m_http_version;

    int m_content_length;
    int m_linger;
    char* m_host;
    //待添加
    
}http_request_t;

typedef struct{

}http_response_t;

void init_http_request(int sockfd,int epollfd,http_request_t* request);
void parse_http_request(void *);


HTTP_CODE do_request(http_request_t*);

#endif
