#include "http_conn.h"
#include <string.h>
#include <errno.h>
#include <string.h>
#include "epoll.h"
#include "utils.h"

const char* ok_200_titile = "OK";
const char* error_400_titile = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_titile = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_titile = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_titile = "Internal Error";
const char* error_500_form = "There was an unsual problem serving the requested file.\n";

const char *doc_root = "../doc";

extern int listenfd;

void init_http_request(int sockfd,int epollfd,http_request_t* request,int backup){
    request->sock_fd = sockfd;
    request->epollfd = epollfd;
    memset(request->m_read_buf,'\0',sizeof(http_response_t));
    //待添加
    request->m_read_idx=0;
    request->m_check_idx=0;
    request->m_start_line=0;
    request->check_state=CHECK_STATE_REQUESTLINE;
    request->m_content_length=0;
    request->m_linger=0;
    request->m_method=GET;
    request->m_url=NULL;
    request->m_http_version=NULL;
    request->m_host=NULL;
    request->m_write_idx=0;
    request->m_iv_count=0;
    request->m_file_address=NULL;
    memset(request->m_write_buf,'\0',WRITE_BUFFSIZE);
    memset(request->m_real_file,'\0',FILE_NAME_LEN);
    if(backup==0){//登录成功后需要保存状态
        request->login_state=0;
        memset(request->username,'\0',USER_NAME_LEN);
        memset(request->m_post_args,'\0',sizeof(request->m_post_args));
    }
}

void close_conn(http_request_t* request){
    if(request->sock_fd!=-1){
        removefd(epfd,request);
        free(request);
    }
}

int read_buf(http_request_t* request){
    if(request->m_read_idx>=READ_BUFFSIZE){
        //缓冲区不足，关闭连接,待优化-------------：写成循环缓冲
        fprintf(stderr,"buffer size is not enough!\n");
        close_conn(request);
        return -1;
    }
    while(1){
        int bytes_read = recv(request->sock_fd,request->m_read_buf+request->m_read_idx,READ_BUFFSIZE-request->m_read_idx,0);
        if(bytes_read==-1){
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                break;
            }
            //读缓冲出错！
            close_conn(request);
            return -1;
        }else if(bytes_read==0){
            return -1;
        }
        request->m_read_idx+=bytes_read;
    }
    return 0;

}

char* get_line(http_request_t* request){
    return request->m_read_buf+request->m_start_line;
}

//从状态机
LINE_STATUS parse_line(http_request_t* request){
    for(;request->m_check_idx<request->m_read_idx;++(request->m_check_idx)){
        char tmp = request->m_read_buf[request->m_check_idx];
        if(tmp=='\r'){
            if(request->m_read_idx+1==request->m_read_idx) return LINE_OK;
            else if(request->m_read_buf[request->m_check_idx+1]=='\n'){
                request->m_read_buf[request->m_check_idx++]='\0';
                request->m_read_buf[request->m_check_idx++]='\0';
                return LINE_OK;
            }
        }else if(tmp=='\n'){
            if(request->m_check_idx>1&&request->m_read_buf[request->m_check_idx-1]=='\r'){
                request->m_read_buf[request->m_check_idx-1]='\0';
                request->m_read_buf[request->m_check_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    } 
    return LINE_OPEN;
}

//获取请求方法(GET、POST) 以及url
HTTP_CODE parse_request_line(http_request_t* request,char* text){
    request->m_url = strpbrk(text," \t"); 
    if(request->m_url==NULL) return BAD_REQUEST;
    *request->m_url++ = '\0';
    char* method = text; //m_url为请求头余下的所有字符串
    if(strcasecmp(method,"GET")==0){
        request->m_method = GET;
    }else if(strcasecmp(method,"POST")==0){
        request->m_method=POST;
    }else return BAD_REQUEST;
    request->m_url+=strspn(request->m_url," \t");
    request->m_http_version = strpbrk(request->m_url," \t");
    if(request->m_http_version==NULL) return BAD_REQUEST;
    *request->m_http_version++ = '\0';
    // if(strcasecmp("HTTP/1.1",request->m_http_version)!=0) return BAD_REQUEST;
    /*
       request->request_headers = strpbrk(request->m_http_version," \n");
       if(request->request_headers==NULL) return BAD_REQUEST;
     *request->request_headers++='\0';*/
    // puts(request->m_http_version);
    if(strncasecmp(request->m_url,"http://",7)==0) {
        request->m_url+=7;
        request->m_url = strchr(request->m_url,'\\');
    }
    if(request->m_url[0]!=47) return BAD_REQUEST;       //此处需要研究，如果写成if(request->m_url[0]!='\\') 条件为真！！！
    //puts(request->m_url);
    request->check_state = CHECK_STATE_HEADER; //有限状态机 
    return NO_REQUEST;
}

HTTP_CODE parse_headers(http_request_t* request,char* text){
    if(text[0]=='\0'){
        if(request->m_content_length!=0){
            request->check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }else if(strcmp(request->m_url,"/")==0){//如果不加主页路径，返回主页
            strcpy(request->m_url,"/index.html");
            request->check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text,"Connection:",11)==0){
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0){
            request->m_linger=1; //keep alive
        }
    }else if(strncasecmp(text,"Content-Length:",15)==0){
        text+=15;
        text+=strspn(text," \t");
        request->m_content_length = atol(text);
    }else if(strncasecmp(text,"Host:",5)==0){
        text+=5;
        text+=strspn(text," \t");
        request->m_host = text;
    }else if(strncasecmp(text,"Content-Type:",13)==0){
        text+=13;
        text+=strspn(text,"\t");
        request->m_content_type = text;
    }
    return NO_REQUEST;
}

HTTP_CODE parse_content(http_request_t* request,char* text){
    if(request->m_read_idx>=(request->m_content_length+request->m_check_idx)){
        text[request->m_content_length]='\0';
        if(request->m_method==POST){//仅为POST请求处理下form data
            request->m_form_data = request->m_read_buf+request->m_start_line;
            return POST_REQUEST;
        }else return GET_REQUEST;
    }
    return NO_REQUEST;
}

HTTP_CODE process_read(http_request_t* request){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE http_code = NO_REQUEST;
    while(((request->check_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||(line_status=parse_line(request))==LINE_OK){
        char* text = get_line(request);
        request->m_start_line = request->m_check_idx;

        switch(request->check_state){
        case CHECK_STATE_REQUESTLINE:
            http_code = parse_request_line(request,text);
            if(http_code==BAD_REQUEST) return BAD_REQUEST;
            break;

        case CHECK_STATE_HEADER:
            http_code = parse_headers(request,text);
            if(http_code == BAD_REQUEST) return BAD_REQUEST;
            break;

        case CHECK_STATE_CONTENT:
            //检查请求体是否被完整
            http_code = parse_content(request,text);
            //puts("check state content");
            if(http_code==GET_REQUEST||http_code==POST_REQUEST) return do_request(request);
            line_status=LINE_OPEN;
            break;

        default:
            return INTERNAL_ERROR;
        }

    }
    return NO_REQUEST;
}

//如果是post请求，根据url确定请求的可执行程序，
//如果是get请求，根据url确定请求的文件
HTTP_CODE do_request(http_request_t* request){
    puts("do request");
    //puts(request->m_read_buf+request->m_start_line);
    strcpy(request->m_real_file,doc_root);
    int len = strlen(doc_root);
    strncpy(request->m_real_file+len,request->m_url,FILE_NAME_LEN-len-1);
    //puts(request->m_real_file);
    //puts(request->m_url);
    if(stat(request->m_real_file,&request->m_file_stat)<0) return NO_RESOURCE;
    if(!(request->m_file_stat.st_mode&S_IROTH)) return FORBIDDEN_REQUEST; //others没有读权限
    if(S_ISDIR(request->m_file_stat.st_mode))//文件夹
        return BAD_REQUEST;
    if(request->m_method==GET){
        int fd = open(request->m_real_file,O_RDONLY);
        request->m_file_address = (char*)mmap(NULL,request->m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
        close(fd);
        return FILE_REQUEST;
    }else if(request->m_method==POST){//POST请求处理
        char* post_msg = request->m_read_buf+request->m_start_line;
        if(strncasecmp(request->m_url+1,"login",5)==0){
            //处理登录的post请求参数
            request->m_post_args[0] = "login";
            size_t len = strspn(post_msg,"username=");
            request->m_post_args[1] = post_msg+len;
            post_msg = strpbrk(post_msg,"&");
            *post_msg++='\0';
            len = strspn(post_msg,"passwd=");
            request->m_post_args[2] = post_msg+len;
            request->m_post_args[3] = NULL;
            //puts("processed post_args");
            return POST_REQUEST;
        }else if(strncasecmp(request->m_url+1,"get_user_info",13)==0){
            request->m_post_args[0] = "get_user_info";
            size_t len = strspn(post_msg,"username=");
            request->m_post_args[1] = post_msg+len;
            request->m_post_args[2] = NULL;

            return POST_REQUEST;
        } 
    }
    return NO_REQUEST;
}

void unmap(http_request_t* request){
    if(request->m_file_address){
        munmap(request->m_file_address,request->m_file_stat.st_size);
        request->m_file_address=NULL;
    }
}

int write_sock(http_request_t* request){
    int bytes_have_send = 0;
    int bytes_to_send = request->m_write_idx;
    if(bytes_to_send==0) {
        modfd(epfd,request,EPOLLIN);
        init_http_request(request->sock_fd,epfd,request,request->login_state);
        return 0;
    }
    while(1){
        int temp = writev(request->sock_fd,request->m_iv,request->m_iv_count);
        if(temp<=-1){
            if(errno==EAGAIN){//写缓冲没有空间
                modfd(epfd,request,EPOLLOUT);
                return 0;
            }
            unmap(request);//取消内存映射
            perror("writev()");
            return -1;
        }
        bytes_to_send -= temp;
        bytes_have_send+=temp;
        if(bytes_to_send<=bytes_have_send){
            puts("http response ok");
            unmap(request);
            if(request->m_linger) {
                //保存旧状态
                init_http_request(request->sock_fd,epfd,request,request->login_state);
                modfd(epfd,request,EPOLLIN);
                return 0;
            }else{//connection: 0
                modfd(epfd,request,EPOLLIN);
                return -1;
            }
        }

    }

}

int add_response(http_request_t* request,const char* format,...){
    if(request->m_write_idx>=WRITE_BUFFSIZE) return -1;
    va_list arg_list;
    va_start(arg_list,format);
    int len = vsnprintf(request->m_write_buf+request->m_write_idx,WRITE_BUFFSIZE-1-request->m_write_idx,
                        format,arg_list);
    if(len >= (WRITE_BUFFSIZE-1-request->m_write_idx)) return -1;
    request->m_write_idx+=len;
    va_end(arg_list);
    return 0;
}

int add_status_line(http_request_t* request,int status,const char* title){
    return add_response(request,"%s %d %s\r\n","HTTP/1.1",status,title);
}

int add_content_len(http_request_t* request,int content_len){
    return add_response(request,"Content-Length: %d\r\n",content_len);
}

int add_linger(http_request_t* request){
    return add_response(request,"Connection: %s\r\n",(request->m_linger==1)?"keep-alive":"close");
}

int add_blank_line(http_request_t* request){
    return add_response(request,"%s","\r\n");
}

int add_content(http_request_t* request,const char* content){
    return add_response(request,"%s",content);
}

void add_headers(http_request_t* request,int content_len){
    add_content_len(request,content_len);
    add_linger(request);
    add_blank_line(request);
}


int process_write(http_request_t* request,HTTP_CODE code){
    int waitid;
    switch(code){
    case INTERNAL_ERROR:
        add_status_line(request,500,error_500_titile);
        add_headers(request,strlen(error_500_form));
        if(add_content(request,error_500_form)==-1) return -1;
        break;
    case BAD_REQUEST:
        add_status_line(request,400,error_400_titile);
        add_headers(request,strlen(error_400_form));
        if(add_content(request,error_400_form)==-1) return -1;
        break;
    case NO_RESOURCE:
        add_status_line(request,404,error_404_titile);
        add_headers(request,strlen(error_404_form));
        if(add_content(request,error_404_form)==-1) return -1;
        break;
    case FORBIDDEN_REQUEST:
        add_status_line(request,403,error_403_titile);
        add_headers(request,strlen(error_403_form));
        if(add_content(request,error_403_form)==-1) return -1;
        break;
    case FILE_REQUEST:
        add_status_line(request,200,ok_200_titile);
        if(request->m_file_stat.st_size!=0){
            add_headers(request,request->m_file_stat.st_size);
            request->m_iv[0].iov_base = request->m_write_buf;
            request->m_iv[0].iov_len = request->m_write_idx;
            request->m_iv[1].iov_base = request->m_file_address;
            request->m_iv[1].iov_len = request->m_file_stat.st_size;
            request->m_iv_count=2;
            return 0;
        }else{//根据请求的url判定是否为文件，且文件存在为空文件
            const char* ok_string = "<html><body></body></html>";
            add_headers(request,strlen(ok_string));
            if(add_content(request,ok_string)==-1) return -1;
        }
    case POST_REQUEST://login采用父子进程的方法，获取用户数据采用函数直接调用的方式,存放在doc里的为空文件
        if(strncasecmp(request->m_url+1,"login",5)==0){//登录
            pid_t pid = fork();
            switch(pid){
            case -1:
                exitErr("func");
                break;
            case 0:
                execvp(request->m_real_file,request->m_post_args);
                puts("execvp err");
                break;
            default://父进程
                wait(&waitid);
                if(waitid>>8==0){//exit_num = waitid>>8
                    //正常登录,正常应该加302以及setcookie，这里自行设计cs架构，就不涉及了
                    request->login_state=1;
                    memcpy(request->username,request->m_post_args[1],strlen(request->m_post_args[1]));
                    add_status_line(request,200,ok_200_titile);
                    add_headers(request,strlen("login successfully!"));
                    if(add_content(request,"login successfully!")==-1) return -1;
                }else{//账号密码错误
                    return process_write(request,BAD_REQUEST);
                }
            }
        }else if(strncasecmp(request->m_url+1,"get_user_info",13)==0){
            puts("process get_user_info");
            if(request->login_state==0){//还未登录
                return process_write(request,FORBIDDEN_REQUEST);
            }else{//查询用户信息
                char* user_info = get_user_info(request->username);
                if(user_info==NULL){
                    fprintf(stderr,"mysql get user_info errr\n");
                    return -1;
                }else{//用户信息获取成功
                    add_status_line(request,200,ok_200_titile);
                    add_headers(request,strlen(user_info));
                    if(add_content(request,user_info)==-1) return -1;
                }
            }
        }else {

        }
        break;
    default:
        return -1;
    }
    request->m_iv[0].iov_base = request->m_write_buf;
    request->m_iv[0].iov_len = request->m_write_idx;
    request->m_iv_count=1;
    return 0;
}

void process(void* arg){
    http_request_t* request = (http_request_t*)arg;
    puts("thread is reading data");
    HTTP_CODE code = NO_REQUEST;
    if(read_buf(request)==0){ //读进缓冲区
        puts(request->m_read_buf);
        code = process_read(request);
        //printf("Host:%s\nConnection:%d\nConten_length:%d\nContent-Type:%s\n\nContent:%s\n",
        //       request->m_host,request->m_linger,request->m_content_length,request->m_content_type,request->m_read_buf+request->m_start_line);

        if(code==BAD_REQUEST) {
            puts("BAD_REQUEST");
            return;
        }
        puts(request->m_url);
        if(process_write(request,code)==-1) {//根据得到的request做出响应
            puts("process write error");
            close_conn(request);
        }
    }
    modfd(epfd,request,EPOLLOUT);
}

