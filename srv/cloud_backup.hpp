#include<cstdio>
#include<string>
#include<vector>
#include<fstream>
#include<iostream>
#include<boost/filesystem.hpp>
#include<unordered_map>
#include<zlib.h>
#include<pthread.h>
#include<boost/algorithm/string.hpp>
#include"httplib.h"

#define NONHOT_TIME 10//定义的非热点文件基准值
#define INTERVAL_TIME 30//非热点的检测时间间隔
#define BACKUP_DIR "./backup/"//文件的备份路径
#define GZFILE_DIR "./gzfile/"//压缩包存放路径
#define DATA_FILE "./list.backup"//数据管理模块的数据备份名称
namespace _cloud_sys
{
    class FileUtil
    {
        public:
            //从文件中读取所有数据
            static bool  Read(const std::string &name,std::string *body)
            {
                std::ifstream fs(name,std::ios::binary);
                if(fs.is_open()==false)
                {
                    std::cout<<" open read file\n";
                    std::cout<<"open file "<<name <<" failed"<<std::endl;
                    return false;
                }
                //获取文件大小
                int64_t fsize=boost::filesystem::file_size(name);
                body->resize(fsize);
                //read(char * buf,size_t size)
                fs.read(&(*body)[0],fsize);//body是指针得先解运用才能使用[]重载
                if(fs.good()==false)
                {
                    std::cout<<"file "<<name<<" read data faild!\n";
                    return false;
                }
                fs.close();
                return true;
            }
            //向文件中写入数据
            static bool Write(const std::string &name,const std::string &body)
            {
                //输出流 ofstream默认打开文件的时候会清空原有的内容，当前策略是覆盖写入 
                std::ofstream ofs(name,std::ios::binary);
                if(ofs.is_open()==false)
                {
                    std::cout<<"写入文件\n";
                    std::cout<<"open file "<<name<<" is failed"<<std::endl;
                    return false;
                }
                ofs.write(&body[0],body.size());
                if(ofs.good()==false)
                {
                    std::cout<<"file "<<name<<" write failed"<<std::endl;
                    return false;
                }
                ofs.close();
                return true;

            }

    };
    class CompressUtil
    {
        public:
            //文件压缩-源文件名称-压缩包名称
            static bool Compress(const std::string &src,const std::string &dst)
            {
                std::string body;
                FileUtil::Read(src,&body);
                gzFile gf=gzopen(dst.c_str(),"wb");//打开压缩包
                if(gf==NULL)
                {
                    std::cout<<"gzopen "<<dst<<" failed\n";
                    return false;
                }
                size_t wlen=0;
                while(wlen<body.size())
                {
                    int ret=gzwrite(gf,&body[wlen],body.size()-wlen);
                    if(ret==0)
                    {
                            std::cout<<"file "<<dst<<" writing compress failed\n";
                            return false;
                    }
                    wlen+=ret;
                }
                gzclose(gf);
                return true;
            }
            //文件解压缩-压缩包名称-存储路径
            static bool UnCompress(const std::string &src,const std::string &dst)
            {
                std::ofstream ofs(dst,std::ios::binary);//默认打开文件的写入偏移量是0
                if(ofs.is_open()==false)
                {
                    std::cout<<"file "<<dst<<" open failed"<<std::endl;
                    return false;
                }
                gzFile gf=gzopen(src.c_str(),"rb");
                if(gf==NULL)
                {
                    std::cout<<"open fail "<<src<<" opne compress file failed\n";
                    ofs.close();
                    return false;
                }
                char tmp[4096]={0};
                int ret=0;
                while((ret=gzread(gf,tmp,4096))>0)
                {
                    ofs.write(tmp,ret);//一直向流中写入数据
                }
                ofs.close();
                gzclose(gf);

                return true;
            }
            
    };
    class DataManager
    {

        public:
            DataManager(const std::string &path):_back_file(path)
        {
            pthread_rwlock_init(&_rwlock,NULL);
        }
            ~DataManager()
            {
                pthread_rwlock_destroy(&_rwlock);
            }
            //判断文件是否存在
            bool Exists(const std::string &name)
            {
                //是否能够从_file_list中找到这个信息
                pthread_rwlock_rdlock(&_rwlock);
                auto it=_file_list.find(name);
                if(it==_file_list.end())
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }

                pthread_rwlock_unlock(&_rwlock);
                return true;
            }
            //判断文件是否已经压缩
            bool IsCompress(const std::string &name)
            {
                //哈希表当中first存储的是源文件名称，second存储的是压缩包名称，
                //文件上传后，源文件名称和压缩包名称一致
                //文件压缩后，将压缩包名称更新为具体的包名
                
                pthread_rwlock_rdlock(&_rwlock); 
                auto it=_file_list.find(name);
                if(it==_file_list.end())
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }
                if(it->first==it->second)
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;//两个文件名一致表示没有压缩
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }
            //获取未压缩的文件列表
			bool NOnCompressList(std::vector<std::string>* list )
            {
                //遍历将没有压缩的文件名添加到list中
                pthread_rwlock_rdlock(&_rwlock);
                auto it=_file_list.begin();
                for(;it!=_file_list.end();++it)
                {
                    if(it->first==it->second)
                    {
                        list->push_back(it->first);
                    }
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;

            }
            //插入/更新数据
            bool Insert(const std::string &src,const std::string &dst)
            {
                pthread_rwlock_wrlock(&_rwlock);
                _file_list[src]=dst;
                pthread_rwlock_unlock(&_rwlock);
                Storage();
                return true;
            }
            //获取所有文件名称
            bool GetAllName(std::vector<std::string>*list)
            {
                 pthread_rwlock_rdlock(&_rwlock);   
                 auto it=_file_list.begin();
                 for(;it!=_file_list.end();++it)
                 {
                    list->push_back(it->first);//获取源文件名称

                 }
                 pthread_rwlock_unlock(&_rwlock);

                 return true;
            }
            //根据源文件名称获取压缩包名称
            bool GETGzname(const std::string &src,std::string *dst)
            {
                auto it=_file_list.find(src);
                if(it==_file_list.end())
                {
                    return false;
                }
                *dst=it->second;
                return true;
            }
            //数据改变后的持久化存储
            bool Storage()
            {
                //将_file_list_中的数据进行持久化存储
                //序列化 src dst\r\n
                std::stringstream tmp;
                pthread_rwlock_rdlock(&_rwlock);//保护_file_list用读锁
                auto it=_file_list.begin();
                for(;it!=_file_list.end();++it)
                {
                    tmp<<it->first<<" "<<it->second<<"\r\n";

                }
                pthread_rwlock_unlock(&_rwlock);
                FileUtil::Write(_back_file,tmp.str());//清空写入，tmp.str(),获取string流里面的string对象
                return true;
            
            }
            //启动时初始化加载原有数据,从持久化存贮的文件中读
            bool InitLoad()
            {
                //1、将这个备份文件从文件找中读取出来
                std::string body;
                if(FileUtil::Read(_back_file,&body)==false)
                {
                    return false;
                }
                //2、进行字符串的处理，按照\r\n进行分割
                std::vector<std::string> list;
                boost::split(list,body,
                        boost::is_any_of("\r\n"),boost::token_compress_off);

                
                //3、每一行按照空格进行分割，前边是key后边是val
                for(auto i:list)
                {
                    size_t pos=i.find(" ");
                    if(pos==std::string::npos)
                    {
                        continue;
                    }
                    std::string key=i.substr(0,pos);
                    std::string val=i.substr(pos+1);
                    Insert(key,val);
                }
                //4、将key和val存储到file_list中
                 
                return true;
            }
        private:
            std::string _back_file;//文件的存储路径
            std::unordered_map<std::string,std::string> _file_list; //文件的持久化存储
             pthread_rwlock_t _rwlock;//读写锁
    };
    DataManager data_manage(DATA_FILE);
  class NonHotCompress
    {
        public:
            NonHotCompress(const std::string gz_dir,const std::string bu_dir):_gz_dir(gz_dir),_bu_dir(bu_dir)
        {}
            //总体向外提供的功能接口，开始压缩模块
            bool Start()
            {
                //是一个循环，持续的过程-》每隔一段时间，判断有没有热点文件，然后进行压缩
                //非热点文件，当前时间减去最后一次访问时间>基准值
                std::cout<<"进入start"<<std::endl;
                while(1)
                {
                    //1、获取一下所有的未压缩文件列表
                    std::vector<std::string > list;
                    data_manage.NOnCompressList(&list);
                    for(auto it=list.begin();it!=list.end();++it)
                    {
                        std::cout<<*it<<std::endl;
                    }
                    //2、逐个判断这个文件是否是热点文件
                    int i=0;
                    for( i=0;i<list.size();++i)
                    {
                        bool ret=FileIsHot(list[i]);
                        std::cout<<ret<<std::endl;
                        if(ret==false)
                        {
                            std::cout<<"have a nonHot flie "<<list[i]<<std::endl;
                            std::string s_filename=list[i];//纯源文件名称
                            std::string d_filename=list[i]+".gz";//纯压缩包名称

                            std::string src_name=_bu_dir+s_filename;//源文件路径名称
                            std::string dst_name=_gz_dir+d_filename;//压缩包路径名称
                            //3、如果是非热点文件，则压缩这个文件，删除源文件
                            if(CompressUtil::Compress(src_name,dst_name)==true)
                            {
                                data_manage.Insert(s_filename,d_filename);//更新数据信息
                                unlink(src_name.c_str());//删除源文件
                            }
                            
                            
                        }
                    }
                    
                    //4、休眠
                    sleep(INTERVAL_TIME);
                }
                return true;

            }	
        private:
            //判断一个文件是否是热点文件
            bool FileIsHot(const std::string name)
            {
                 //当前时间减去最后一次访问时间>设定的时间就是非热点
                 time_t cur_t =time(NULL);
                 struct stat st;
                 if(stat(name.c_str(),&st)<0)
                 {
                     std::cout<<"file"<<name<<"stat failed\n";
                     return false;
                 }
                 if((cur_t - st.st_atime) > NONHOT_TIME)
                 {
                     return false;
                 }
                 return true;//设定时间以内的都是热点文件 
                 
            }
            std::string _bu_dir;//源文件的所在路径 
            std::string _gz_dir;//压缩后的文件存储路径
    };

    class Server
    {
        public:
            bool Start()
            {
                _server.Put("/(.*)",Upload);
                _server.Get("/list",List);
                _server.Get("/download/(.*)",Download);
                //(.*)正则表达式匹配/download/后的任意字符,为了避免有文件名字叫list与上边list请求混淆
                //在路由表中添加了三个请求回调函数，针对什么样的请求回调什么样的方法
               _server.listen("0.0.0.0",9000);
                return true;

            }//启动网络通信模块
        private:
            //文件上传请求 
            static void Upload(const httplib::Request & req,httplib::Response &rsp)
            {
               /* rsp.status=200;
                rsp.set_content("upload",6,"text/html");
            */
                std::string filename=req.matches[1];
                std::string pathname=BACKUP_DIR+filename;
                FileUtil::Write(pathname,req.body);//向备份文件夹中写入数据，文件不存在则创建
                data_manage.Insert(filename,filename);
                rsp.status=200;
                return;
            }

            //文件列表请求
            static void List(const httplib::Request & req,httplib::Response &rsp)
            {
              /*  rsp.status=200;
               // set_content(正文数据，正文数据长度，正文类型-Content-Type)
               // rsp.body=upload;
               //rsp.set_header("content-Type","text/html");
               
               rsp.set_content("list",4,"text/html");
                */
                std::vector<std::string> list;
                data_manage.GetAllName(&list);
                std::stringstream tmp;
                tmp<<"<html><body><hr />";
                for(int i=0;i<list.size();++i)
                {
                    tmp<<"<a href='/download/"<<list[i]<<"'>"<<list[i]<<"</a>";
                    //tmp<<"<a href='/download/a.txt'>"<<"a.txt"<< "</a>"
                    tmp<<"<hr />";
                }
                tmp<<"<hr /></body></html>";
                 rsp.set_content(tmp.str().c_str(),tmp.str().size(),"text/html");
                 rsp.status=200;
                return ;

            }
            //文件下载处理回调函数
            static void Download(const httplib::Request & req,httplib::Response &rsp)
            {
                std::string filename =req.matches[1];
                if(data_manage.Exists(filename)==false)
                {
                    rsp.status=404;
                    return ;
                }
                std::string pathname =BACKUP_DIR+filename;
                if(data_manage.IsCompress(filename)==true)
                {

                    std::string gzfile;
                    data_manage.GETGzname(filename,&gzfile);
                    std::string gzpathname=GZFILE_DIR+gzfile;
                    
                    CompressUtil::UnCompress(gzpathname,pathname);
                    data_manage.Insert(filename,filename);
                    unlink(gzpathname.c_str());

                }
                
                    FileUtil::Read(pathname,&rsp.body);
                    rsp.set_header("Content-Type","application/octet-stream");//二进制流文件下载
                    rsp.status=200;
                    return ;
            }
        private:
            std::string _file_dir;//文件上传备份路径
            httplib::Server _server;


    };

}
